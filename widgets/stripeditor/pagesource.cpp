#include "pagesource.h"
#include "layout.h"

#include <QDebug>
#include <QFutureWatcher>
#include <QImage>
#include <QStringList>
#include <QtConcurrent>

#include <platemaker/core/colour_corrector/colour_corrector.hpp>
#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <algorithm>
#include <string>

namespace StripEdit {

namespace {

//! LRU caps (in KiB). A scaled page is big — 800×5120 RGBA is ~16 MiB — so these hold only a handful,
//! which is the point: RAM tracks the viewport plus the prefetch margin, not the chapter length.
constexpr int k_pageCacheKiB  = 96 * 1024;   //!< ~6 scaled pages: visible + prefetch, with headroom.
constexpr int k_proxyCacheKiB = 24 * 1024;   //!< Hundreds of 200px-wide proxies.

} // namespace

PageSource::PageSource(const Layout& layout, QObject* parent)
    : QObject(parent)
    , m_layout(layout)
{
    m_pageCache.setMaxCost(k_pageCacheKiB);
    m_proxyCache.setMaxCost(k_proxyCacheKiB);
    m_gradedCache.setMaxCost(k_pageCacheKiB); // a graded preview is the same size as the page it came from
}

bool PageSource::setFeed(const std::vector<Platemaker::Models::InputFile>&     inputs,
                         const Platemaker::Models::OutputProfile&              outProfile,
                         const std::vector<Platemaker::Models::CanvasProfile>& canvasProfiles,
                         const std::vector<std::string>&                       canvasProfileIds,
                         const QString&                                        cacheDir)
{
    QStringList parts;
    parts << QString::fromStdString(Platemaker::Models::outputProfileSignature(outProfile))
          << cacheDir;
    for (const auto& id : canvasProfileIds)
        parts << QString::fromStdString(id);
    for (const auto& cp : canvasProfiles)
        parts << QStringLiteral("%1:%2").arg(QString::fromStdString(cp.id),
                                             QString::fromStdString(
                                                 Platemaker::Models::canvasRenderFingerprint(cp)));
    for (const auto& in : inputs)
        parts << QStringLiteral("%1:%2").arg(
                     QString::fromStdString(in.filePath),
                     in.status == Platemaker::Models::FileStatus::Missing ? QStringLiteral("x")
                                                                         : QStringLiteral("."));
    const QString sig = parts.join(QStringLiteral("/"));

    if (sig == m_feedSignature)
        return false;               // same strip — the built pages stay valid

    m_feedSignature    = sig;
    m_inputs           = inputs;
    m_outProfile       = outProfile;
    m_canvasProfiles   = canvasProfiles;
    m_canvasProfileIds = canvasProfileIds;
    m_cacheDir         = cacheDir;
    return true;
}

std::vector<Platemaker::Core::PagePreviewGeometry> PageSource::layoutPages() const
{
    try {
        return Platemaker::Core::ProcessingPipeline::layoutPagesFromHeaders(
            m_inputs, m_outProfile, m_canvasProfiles, m_canvasProfileIds);
    } catch (const std::exception& e) {
        qWarning() << "StripEdit::PageSource: preview layout failed —" << e.what();
        return {};
    }
}

void PageSource::reset()
{
    ++m_generation;             // in-flight results from before now are ignored on arrival
    m_pageCache.clear();
    m_proxyCache.clear();
    m_gradedCache.clear();
    m_pageInFlight.clear();
    m_proxyInFlight.clear();
}

QPixmap PageSource::pageOf(int index) const
{
    const QPixmap *p = m_pageCache.object(index);
    return p ? *p : QPixmap();
}

QPixmap PageSource::proxyOf(int index) const
{
    const QPixmap *p = m_proxyCache.object(index);
    return p ? *p : QPixmap();
}

QPixmap PageSource::gradedOf(int index) const
{
    const QPixmap* p = m_gradedCache.object(index);
    return p ? *p : QPixmap();
}

// ---------------------------------------------------------------------------
// Lazy page build: proxy (blurry, instant) + the real page domain (sharp, async)
// ---------------------------------------------------------------------------

void PageSource::request(int index)
{
    const int gen = m_generation;

    // Sharp tier: put the input page through the library's page domain on a worker thread. This is the
    // same code the render runs, so the pixels here are the pixels the render will produce — ungraded,
    // because the grade is a point op we apply to the result and re-apply on every slider move.
    if (!m_pageCache.object(index) && !m_pageInFlight.contains(index)) {
        m_pageInFlight.insert(index);
        // Copies, because the worker outlives this call and the workspace can change under it.
        const auto  input   = m_inputs[static_cast<std::size_t>(m_layout.page(index).inputIndex)];
        const auto  outProf = m_outProfile;
        const auto  profs   = m_canvasProfiles;
        const auto  ids     = m_canvasProfileIds;
        const QSize size    = m_layout.page(index).size;

        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, index, gen] {
            const QImage img = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation)   // superseded by a rebuild — its in-flight set was already cleared
                return;
            m_pageInFlight.remove(index);
            if (img.isNull())
                return;
            const QPixmap pm = QPixmap::fromImage(img);
            m_pageCache.insert(index, new QPixmap(pm),
                               qMax(1, (pm.width() * pm.height() * 4) / 1024));
            produceGraded(index); // grade the freshly-built page if the grade is on
            emit pageReady(index);
        });
        watcher->setFuture(QtConcurrent::run(
            [input, outProf, profs, ids, size]() -> QImage {
                QImage img(size, QImage::Format_RGBA8888);
                // decodePageToRgba writes tightly packed RGBA8888. Format_RGBA8888 is 4 bytes per pixel,
                // so a scanline is always 4-byte aligned and Qt adds no padding — but assert rather than
                // assume, because a padded scanline would shear the image.
                if (img.bytesPerLine() != size.width() * 4)
                    return {};
                try {
                    Platemaker::Core::ProcessingPipeline::decodePageToRgba(
                        input, outProf, profs, ids, img.bits(), size.width(), size.height());
                } catch (...) {
                    return {};   // the page stays on its proxy; the layout already knows its size
                }
                return img;
            }));
    }

    // Proxy tier: the input page's thumbnail, reusing the lib ThumbnailCache the Input tab already warms
    // for exactly these files. Aspect-wrong for a margin-cropped page, but it is a placeholder that gets
    // replaced the moment the real page arrives.
    if (!m_cacheDir.isEmpty() && !m_proxyCache.object(index) && !m_proxyInFlight.contains(index)) {
        m_proxyInFlight.insert(index);
        const std::string path     = m_layout.page(index).sourcePath.toStdString();
        const std::string cacheDir = m_cacheDir.toStdString();
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, index, gen] {
            const QString thumbPath = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation)   // superseded by a rebuild — its in-flight set was already cleared
                return;
            m_proxyInFlight.remove(index);
            if (thumbPath.isEmpty())
                return;
            const QPixmap pm(thumbPath);
            if (pm.isNull())
                return;
            m_proxyCache.insert(index, new QPixmap(pm),
                                qMax(1, (pm.width() * pm.height() * 4) / 1024));
            emit pageReady(index);
        });
        watcher->setFuture(QtConcurrent::run([path, cacheDir]() -> QString {
            try {
                Platemaker::Infrastructure::ThumbnailCache cache(cacheDir);
                return QString::fromStdString(cache.getOrGenerate(path));
            } catch (...) {
                return {};
            }
        }));
    }
}

// ---------------------------------------------------------------------------
// Colour correction
// ---------------------------------------------------------------------------

bool PageSource::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    const std::string sig = Platemaker::Models::processingConfigSignature(cc, {});
    if (sig == m_ccSignature)
        return false;
    m_ccSignature = sig;

    // The grade is a point operation on already-built pixels and never changes how a page is read, so
    // no grade edit can invalidate a built page — re-grading the resident ones is always enough.
    m_cc = cc;
    return true;
}

bool PageSource::gradeActive() const
{
    // Independent of the active tool: the strip's pixels are the ungraded input, so the project's grade
    // is what the strip is *supposed* to look like — switching to Pan must not reveal an ungraded strip.
    if (!m_cc.enabled)
        return false;
    // A neutral grade leaves the pixels unchanged — nothing to preview.
    return !(m_cc.brightness == 0.0 && m_cc.contrast == 1.0 && m_cc.saturation == 1.0
             && !Platemaker::Models::hasAnyCurve(m_cc.curves));
}

void PageSource::produceGraded(int index)
{
    if (!gradeActive() || m_gradedCache.object(index))
        return;
    const QPixmap* src = m_pageCache.object(index); // grade from the resident (ungraded) page
    if (!src)
        return;

    // A page excluded from the grade renders ungraded — the same rule the render applies, and the
    // reason the preview's unit of work is the page: an output slice can straddle an excluded and an
    // included page, so on that feed the exclusion could not be honoured at display time at all.
    const auto&       ex  = m_cc.excludedInputUids;
    const std::string uid = m_layout.anchorUidForPage(index).toStdString();
    if (std::find(ex.begin(), ex.end(), uid) != ex.end())
        return;

    QImage img = src->toImage().convertToFormat(QImage::Format_RGBA8888);
    try {
        // ponytail: grades the whole page (~4 Mpx at 800×5120) even though the viewport shows a
        // fraction of it. Simple and cache-friendly — one grade per page, none while scrolling within
        // it. If a slider drag feels heavy, grade only the visible band: rows are contiguous in an
        // interleaved RGBA buffer, so applyToRgba(bits + top*w*4, w, rows, cc) is already legal.
        Platemaker::Core::ColourCorrector{}.applyToRgba(img.bits(), img.width(), img.height(), m_cc);
    } catch (const std::exception& e) {
        qWarning() << "StripEdit::PageSource: grade preview failed for page" << index << "—" << e.what();
        return; // leave it ungraded (paint falls back to the built page)
    } catch (...) {
        qWarning() << "StripEdit::PageSource: grade preview failed for page" << index;
        return;
    }
    const QPixmap g = QPixmap::fromImage(img);
    m_gradedCache.insert(index, new QPixmap(g), qMax(1, (g.width() * g.height() * 4) / 1024));
    emit pageReady(index);
}

}  // namespace StripEdit
