#ifndef STRIPEDIT_STRIPSTATEPANEL_H
#define STRIPEDIT_STRIPSTATEPANEL_H

#include <QSize>
#include <QString>
#include <QWidget>

#include <platemaker/models/colour_correction.hpp>

class QCheckBox;
class QLabel;

namespace StripEdit {

/**
 * @brief What the strip, or one of its pages, *is* — ③ for the subjects that are not overlays.
 *
 * The same surface as `ObjectStatePanel`, and the same question — what is selected — asked of different
 * things. It is a panel of its own rather than more sections in that one because nothing here is a
 * `TextArtifact`: a page has no fill and no tail, and pretending otherwise would mean a panel full of
 * sections that are always hidden.
 *
 * **The strip** is summarised: how many pages it has, how many the grade skips, and which colour
 * adjustments are applied to it, named the way GIMP names them. **A page** is named, sized, and carries
 * the one colour decision the library lets a page make — whether the strip's grade skips it.
 */
class StripStatePanel : public QWidget
{
    Q_OBJECT

public:
    explicit StripStatePanel(QWidget* parent = nullptr);

    //! The strip: its page count, how many of those the grade skips, and the grade itself.
    void showStrip(int pageCount, int excludedCount, const Platemaker::Models::ColourCorrection& cc);

    /**
     * @brief One page of the strip.
     *
     * @param stripGraded Whether the strip carries any grade at all. Excluding a page from a grade that
     *                    changes nothing is allowed and remembered, and the panel says it has no effect
     *                    yet rather than leaving a checkbox that appears to do nothing.
     */
    void showPage(const QString& inputUid, const QString& label, QSize sizeInStrip, bool excluded,
                  bool stripGraded);

signals:
    //! The artist changed whether page @p inputUid is skipped by the strip's grade.
    void excludedToggled(const QString& inputUid, bool excluded);

private:
    QLabel*    m_subject  = nullptr;   //!< Names the strip or the page.
    QLabel*    m_details  = nullptr;   //!< What it is: counts, size, applied adjustments.
    QCheckBox* m_excluded = nullptr;   //!< A page's one colour decision.
    QLabel*    m_note     = nullptr;   //!< What that decision does, right now.
    QString    m_pageUid;              //!< The page on show, when it is a page.
    bool       m_populating = false;   //!< Keeps showPage() from reporting its own checkbox update.
};

}  // namespace StripEdit

#endif // STRIPEDIT_STRIPSTATEPANEL_H
