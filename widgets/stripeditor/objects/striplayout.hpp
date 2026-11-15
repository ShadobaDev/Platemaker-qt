#ifndef STRIPEDIT_STRIPLAYOUT_HPP
#define STRIPEDIT_STRIPLAYOUT_HPP

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/models/processing_steps.hpp>
#include <platemaker/models/project_item.hpp>

#include <optional>
#include <vector>

namespace StripEdit {

/**
 * \brief One drawable page in the strip: where it sits, what it is, and what an overlay anchors to.
 *
 * "Drawable" excludes the pages a render would skip (missing or unreadable). They are dropped when the
 * layout is built, which is what keeps every page below them at the offset the render will give it.
 */
struct Page
{
    int     inputIndex = -1;  //!< Index into the feed's inputs — the page still needs its InputFile to build.
    QString sourcePath;       //!< Source file, for the proxy thumbnail lookup and diagnostics.
    QString inputUid;         //!< Stable page identity; what StripOverlay::anchorInputUid points at.
    int     top = 0;          //!< Cumulative Y offset in strip coordinates.
    QSize   size;             //!< Scaled size, as layoutPagesFromHeaders reported it.
};

/**
 * \brief Where every page lands in the strip, and the queries the editor asks of that.
 *
 * Split out of the editor, where it lived as **four parallel QLists** indexed in lockstep — a struct
 * wearing four names, and one `append()` away from silently mismatching. It is also the part of the
 * editor with no Qt widget in it: no scene, no view, no palette, nothing to construct. That makes it the
 * one piece testable on its own, and it is the piece the overlay code interrogates constantly (every
 * placement, every re-anchor, every orphan check).
 *
 * **Strip coordinates are scene coordinates, 1:1**, so what this returns is directly usable as a scene
 * position — which is exactly why overlays need no coordinate-mapping layer of their own.
 */
class StripLayout
{
public:
    /**
     * \brief Adopts a fresh layout from the library, dropping pages a render would skip.
     *
     * \param geometry `ProcessingPipeline::layoutPagesFromHeaders()` output — header reads, no pixels.
     * \param inputs   The feed, in strip order; supplies each page's uid and its index back into itself.
     */
    void build(const std::vector<Platemaker::Core::PagePreviewGeometry>& geometry,
               const std::vector<Platemaker::Models::InputFile>&         inputs);

    void clear();

    [[nodiscard]] bool  isEmpty() const { return m_pages.isEmpty(); }
    [[nodiscard]] int   pageCount() const { return m_pages.size(); }
    [[nodiscard]] QSize stripSize() const { return {m_width, m_height}; }
    [[nodiscard]] int   stripWidth() const { return m_width; }
    [[nodiscard]] int   stripHeight() const { return m_height; }

    //! Page \p i. Out of range gives a default-constructed page rather than throwing: every caller is a
    //! paint or a hit-test, and an empty rect is a better answer there than an exception.
    [[nodiscard]] const Page& page(int i) const;

    //! Scene rect of page \p i — the strip is one column, so x is always 0.
    [[nodiscard]] QRectF pageRect(int i) const;

    /**
     * \brief Index of the page containing strip-Y \p y.
     *
     * Clamps to the first page above the strip rather than returning -1, because the only caller that
     * matters is overlay placement: an anchored overlay is the only kind this editor should create, and
     * an absolute one silently drifts onto different artwork as soon as a page above it changes height.
     * Returns -1 only when there is no layout at all.
     */
    [[nodiscard]] int pageAtSceneY(qreal y) const;

    /**
     * \brief The width every page was scaled to — the unit every overlay fraction is measured in.
     *
     * The same number the render passes to \c Models::resolveOverlayAnchors(). It is the strip's width
     * by construction: the page domain scales every page to the output profile's target width, so the
     * widest page *is* that width.
     */
    [[nodiscard]] int   targetWidth() const { return m_width; }
    //! One fraction of the target width, in strip pixels.
    [[nodiscard]] qreal pixels(double frac) const { return frac * m_width; }
    //! A length in strip pixels, as a fraction of the target width; 0 with no layout. pixels()' inverse.
    [[nodiscard]] double fraction(qreal pixels) const { return m_width > 0 ? pixels / m_width : 0.0; }

    //! Input uid of page \p page, empty when out of range.
    [[nodiscard]] QString anchorUidForPage(int page) const;

    //! Page carrying input uid \p uid, or -1 — which is what makes an overlay an orphan.
    [[nodiscard]] int pageForAnchor(const QString& uid) const;

    /**
     * \brief Scene position of \p o, resolving its page anchor against this layout.
     *
     * The same arithmetic `Models::resolveOverlayAnchors()` performs at render time, which is why the
     * preview cannot disagree with the render about where a bubble lands. An unanchored overlay — an
     * older workspace, or one whose page is gone — is already in strip coordinates and passes through.
     */
    [[nodiscard]] QPointF scenePosOf(const Platemaker::Models::StripOverlay& o) const;

    //! Where an overlay sits: the input uid of the page it is anchored to, and its top-left and width in
    //! fractions of the target width — what a workspace stores.
    struct Anchored
    {
        QString anchor;
        double  xFrac = 0.0;
        double  yFrac = 0.0;
        double  wFrac = 0.0;
    };
    /**
     * \brief An overlay drawn from scene point @p topLeft, @p width strip pixels wide, anchored to the page under
     *        @p pageY (its top-left's when none is given): scenePosOf()'s inverse, and what placing, importing
     *        and moving one write. None without a layout.
     */
    [[nodiscard]] std::optional<Anchored> anchoredAt(const QPointF& topLeft, qreal width,
                                                     std::optional<qreal> pageY = std::nullopt) const;
    //! The same, kept on the page of input uid @p anchor; measured from the strip's top when no page has it.
    [[nodiscard]] Anchored anchoredOn(const QString& anchor, const QPointF& topLeft, qreal width) const;
    /**
     * \brief The scale an overlay @p naturalWidth pixels wide on its own is drawn at: its width fraction over that.
     *
     * wFrac 0 means "its own size", which is exactly a scale of 1 — and it is what an overlay placed before
     * sizes were recorded, or imported and never resized, carries.
     */
    [[nodiscard]] qreal scaleOf(const Platemaker::Models::StripOverlay& o, qreal naturalWidth) const;
    //! A picture @p naturalWidth pixels wide, drawn @p wFrac of the target width: in per cent of its own size.
    [[nodiscard]] double percentOf(double wFrac, qreal naturalWidth) const;

private:
    QList<Page> m_pages;
    int              m_width  = 0;   //!< Widest page = strip width.
    int              m_height = 0;   //!< Sum of page heights.
};

}  // namespace StripEdit

#endif // STRIPEDIT_STRIPLAYOUT_HPP
