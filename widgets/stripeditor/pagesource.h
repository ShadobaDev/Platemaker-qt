#ifndef STRIPEDIT_PAGESOURCE_H
#define STRIPEDIT_PAGESOURCE_H

#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/processing_steps.hpp>
#include <platemaker/models/project_item.hpp>

#include <string>
#include <vector>

namespace StripEdit {

class Layout;

/**
 * @brief "Give me page \p n at the best fidelity available." — the strip's page memory.
 *
 * One sentence, no "and": everything about *how a page becomes pixels* lives here — the feed the
 * library needs, the blurry-proxy and sharp tiers, the graded previews, the LRU caps that keep RAM
 * tracking the viewport rather than the chapter, and the generation counter that makes a rebuild
 * discard results still in flight. The editor above it only ever asks for a page and repaints when
 * one arrives.
 *
 * It holds the \c Layout by reference rather than owning it: page geometry is what the *editor*
 * builds (and what every overlay placement question is asked of), while this class only needs to know
 * which input a page index maps to and how big it is.
 */
class PageSource : public QObject
{
    Q_OBJECT

public:
    explicit PageSource(const Layout& layout, QObject* parent = nullptr);

    /**
     * @brief Adopts a new feed, and says whether it is actually a different strip.
     *
     * Everything that can move a page on the strip, and nothing else, goes into a signature. The owner
     * refreshes the editor on any project edit — a settled slider drag included — and a rebuild drops
     * every built page, so an unconditional rebuild would re-fetch the visible pages after each of
     * them. Deliberately excluded: per-input render bookkeeping (status, sha, timestamps), which the
     * render stamps without any page moving. A page edited on disk therefore does not refresh by
     * itself; re-opening the editor or rendering picks it up.
     *
     * @return \c true when the feed changed and the strip must be rebuilt.
     */
    bool setFeed(const std::vector<Platemaker::Models::InputFile>&     inputs,
                 const Platemaker::Models::OutputProfile&              outProfile,
                 const std::vector<Platemaker::Models::CanvasProfile>& canvasProfiles,
                 const std::vector<std::string>&                       canvasProfileIds,
                 const QString&                                        cacheDir);

    /**
     * @brief Where each page lands, from the library's page domain — header reads only, no decoding.
     *
     * The numbers come from the same code a render uses, so the strip laid out from this is the strip
     * a render would build, before any render exists.
     */
    [[nodiscard]] std::vector<Platemaker::Core::PagePreviewGeometry> layoutPages() const;

    //! The feed's inputs — the Layout needs them to turn the geometry above into drawable pages.
    [[nodiscard]] const std::vector<Platemaker::Models::InputFile>& inputs() const { return m_inputs; }

    //! Where the render will cut, which is what the editor draws its seam guides at.
    [[nodiscard]] int sliceHeight() const { return m_outProfile.sliceHeight; }

    //! Discards all cached/in-flight pages and bumps the generation so stale results are ignored.
    void reset();

    //! Kicks off the async proxy + page build for one page (no-op if already cached / in flight).
    void request(int index);

    [[nodiscard]] QPixmap pageOf(int index) const;   //!< Built, ungraded. Null until it arrives.
    [[nodiscard]] QPixmap proxyOf(int index) const;  //!< Blurry stand-in. Null until it arrives.
    [[nodiscard]] QPixmap gradedOf(int index) const; //!< Graded preview. Null when the grade is off.

    /**
     * @brief Adopts a grade, and says whether it actually changed anything.
     *
     * The same grade arrives repeatedly — the owner re-feeds on every project edit, and the panel
     * persists a settled drag while still emitting live values — so re-grading for it would double the
     * work of a slider drag. \c processingConfigSignature() is the library's own fingerprint of this
     * exact config; it is the equality test rather than a hand-rolled field compare. (It is empty for
     * any disabled grade, which is right: nothing is graded and the load path is the same one.)
     */
    bool setColourCorrection(const Platemaker::Models::ColourCorrection& cc);

    //! True when the grade would visibly change a pixel. Independent of the active tool.
    [[nodiscard]] bool gradeActive() const;

    //! Grade the built page \p index into the graded cache (no-op if grade inactive / page not built).
    void produceGraded(int index);

    //! The grade changed → previous previews are stale.
    void clearGraded() { m_gradedCache.clear(); }

signals:
    //! Page \p index now has better pixels than it did — repaint it.
    void pageReady(int index);

private:
    //! Page geometry, owned by the editor. Only read here: which input, how big, which file.
    const Layout& m_layout;

    // --- the feed: everything the lib needs to put one input page through the page domain ---
    std::vector<Platemaker::Models::InputFile>     m_inputs;           //!< Project inputs in strip order.
    Platemaker::Models::OutputProfile              m_outProfile;       //!< Target width + slice height.
    std::vector<Platemaker::Models::CanvasProfile> m_canvasProfiles;   //!< Workspace palette (margins).
    std::vector<std::string>                       m_canvasProfileIds; //!< Profiles linked to this project.
    QString                                        m_cacheDir;         //!< Proxy-thumbnail cache dir.
    QString                                        m_feedSignature;    //!< Fingerprint of the feed above — a re-feed that matches it keeps the built pages.

    // --- async build state ---
    QCache<int, QPixmap> m_pageCache;        //!< Built (ungraded) pages, LRU-evicted under a byte cap.
    QCache<int, QPixmap> m_proxyCache;       //!< Blurry proxy thumbnails, LRU-evicted under a byte cap.
    QSet<int>            m_pageInFlight;     //!< Page indices whose build is running.
    QSet<int>            m_proxyInFlight;    //!< Page indices whose proxy load is running.
    int                  m_generation = 0;   //!< Bumped on every rebuild; async results from an older gen are dropped.

    // --- colour correction ---
    Platemaker::Models::ColourCorrection m_cc;            //!< Current grade (from the project / the panel).
    std::string                          m_ccSignature;   //!< Fingerprint of m_cc — a grade that matches it is ignored.
    QCache<int, QPixmap>                 m_gradedCache;   //!< Graded preview of visible pages; cleared on grade change.
};

}  // namespace StripEdit

#endif // STRIPEDIT_PAGESOURCE_H
