#ifndef STRIPEDIT_STRIPSTATEPANEL_HPP
#define STRIPEDIT_STRIPSTATEPANEL_HPP

#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

#include <platemaker/models/colour_correction.hpp>

#include "colouradjustment.hpp"

class QCheckBox;
class QLabel;
class QVBoxLayout;

namespace StripEdit {

/**
 * @brief What the strip, or one of its pages, *is* — ③ for the subjects that are not overlays.
 *
 * The same surface as `ObjectStatePanel`, and the same question — what is selected — asked of different
 * things. It is a panel of its own rather than more sections in that one because nothing here is a
 * `Artifact`: a page has no fill and no tail, and pretending otherwise would mean a panel full of
 * sections that are always hidden.
 *
 * **The strip** lists its pages and **the colour adjustments applied to it**, the way an image editor lists the
 * filters on a layer: each can be reopened in the Grade tool or removed, and removing one leaves the others
 * and every page exclusion alone. **A page** is named, sized, and carries the one colour decision the
 * library lets a page make — whether the strip's grade skips it.
 */
class StripStatePanel : public QWidget
{
    Q_OBJECT

public:
    explicit StripStatePanel(QWidget* parent = nullptr);

    //! The strip: its page count, how many of those the grade skips, and what the grade applies.
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
    //! Reopen @p adjustment in the Grade tool.
    void adjustmentEditRequested(StripEdit::ColourAdjustment adjustment);
    //! Take @p adjustment off the strip.
    void adjustmentRemoveRequested(StripEdit::ColourAdjustment adjustment);

private:
    //! Rebuilds the adjustment rows when *which* adjustments are applied has changed; otherwise only their
    //! values are rewritten. The Grade tool's sliders feed this panel on every tick, and rows rebuilt at that
    //! rate would flicker under the cursor.
    void showAdjustments(const Platemaker::Models::ColourCorrection& cc);

    QLabel*      m_subject  = nullptr;   //!< Names the strip or the page.
    QLabel*      m_details  = nullptr;   //!< What it is: counts or size.
    QLabel*      m_adjustmentsHeading = nullptr;
    QWidget*     m_adjustments        = nullptr;   //!< One row per applied adjustment, or "none".
    QVBoxLayout* m_adjustmentsLayout  = nullptr;
    QCheckBox*   m_excluded = nullptr;   //!< A page's one colour decision.
    QLabel*      m_note     = nullptr;   //!< What that decision does, right now.

    QList<ColourAdjustment> m_shown;          //!< The adjustments the current rows stand for, in order.
    QHash<int, QLabel*>     m_valueLabels;    //!< Each row's text, by adjustment, rewritten in place.
    bool                    m_rowsBuilt = false;
    QString                 m_pageUid;        //!< The page on show, when it is a page.
    bool                    m_populating = false;   //!< Keeps showPage() from reporting its own update.
};

}  // namespace StripEdit

#endif // STRIPEDIT_STRIPSTATEPANEL_HPP
