#include "stripstatepanel.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace StripEdit {

namespace {

[[nodiscard]] QString rowText(const Platemaker::Models::ColourCorrection& cc, ColourAdjustment a)
{
    const QString values = colourAdjustmentValues(cc, a);
    return values.isEmpty() ? colourAdjustmentName(a)
                            : QObject::tr("%1 — %2").arg(colourAdjustmentName(a), values);
}

}  // namespace

StripStatePanel::StripStatePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);

    // Named the way ObjectStatePanel names its subject, so the two read as one surface.
    m_subject = new QLabel(this);
    m_subject->setTextFormat(Qt::PlainText);
    QFont bold = m_subject->font();
    bold.setBold(true);
    m_subject->setFont(bold);
    lay->addWidget(m_subject);

    m_details = new QLabel(this);
    m_details->setTextFormat(Qt::PlainText);
    m_details->setWordWrap(true);
    lay->addWidget(m_details);

    m_adjustmentsHeading = new QLabel(tr("Colour correction"), this);
    m_adjustmentsHeading->setFont(bold);
    lay->addWidget(m_adjustmentsHeading);

    m_adjustments       = new QWidget(this);
    m_adjustmentsLayout = new QVBoxLayout(m_adjustments);
    m_adjustmentsLayout->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_adjustments);

    m_excluded = new QCheckBox(tr("Excluded from colour correction"), this);
    m_excluded->setToolTip(tr("A title or credits page usually wants its own colours. The page is "
                              "rendered exactly as if the strip had no grade at all."));
    lay->addWidget(m_excluded);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setEnabled(false);   // reads as secondary without a hardcoded colour
    lay->addWidget(m_note);

    lay->addStretch(1);

    connect(m_excluded, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_populating && !m_pageUid.isEmpty())
            emit excludedToggled(m_pageUid, on);
    });
}

void StripStatePanel::showStrip(int pageCount, int excludedCount,
                                const Platemaker::Models::ColourCorrection& cc)
{
    m_pageUid.clear();
    m_subject->setText(tr("Strip"));
    m_details->setText(excludedCount > 0
                           ? tr("%n page(s)", "", pageCount) + QStringLiteral(" · ")
                                 + tr("%n excluded from colour correction", "", excludedCount)
                           : tr("%n page(s)", "", pageCount));

    showAdjustments(cc);

    m_adjustmentsHeading->setVisible(true);
    m_adjustments->setVisible(true);
    m_excluded->setVisible(false);
    m_note->setVisible(false);
}

void StripStatePanel::showAdjustments(const Platemaker::Models::ColourCorrection& cc)
{
    QList<ColourAdjustment> applied;
    for (ColourAdjustment a : allColourAdjustments())
        if (isColourAdjustmentApplied(cc, a))
            applied << a;

    // Same adjustments as the rows already stand for: rewrite their values and leave the widgets alone.
    if (m_rowsBuilt && applied == m_shown) {
        for (ColourAdjustment a : applied)
            if (QLabel* label = m_valueLabels.value(static_cast<int>(a)))
                label->setText(rowText(cc, a));
        return;
    }

    // Rebuilt — and the old rows are let go later rather than now: one of them may hold the button whose
    // click brought us here, and deleting a widget from inside its own signal is how a panel crashes.
    while (QLayoutItem* item = m_adjustmentsLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    m_valueLabels.clear();
    m_shown     = applied;
    m_rowsBuilt = true;

    if (applied.isEmpty()) {
        auto* none = new QLabel(tr("None — every page renders in its own colours."), m_adjustments);
        none->setWordWrap(true);
        m_adjustmentsLayout->addWidget(none);
        return;
    }

    for (ColourAdjustment a : applied) {
        auto* row = new QWidget(m_adjustments);
        auto* h   = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);

        auto* label = new QLabel(rowText(cc, a), row);
        label->setWordWrap(true);
        h->addWidget(label, 1);
        m_valueLabels.insert(static_cast<int>(a), label);

        if (isColourAdjustmentEditable(a)) {
            auto* edit = new QToolButton(row);
            edit->setText(tr("Edit"));
            edit->setToolTip(tr("Open it in the Grade tool."));
            connect(edit, &QToolButton::clicked, this, [this, a] { emit adjustmentEditRequested(a); });
            h->addWidget(edit);
        }
        auto* remove = new QToolButton(row);
        remove->setText(tr("Remove"));
        remove->setToolTip(tr("Takes this adjustment off the strip. The others stay, and so do page "
                              "exclusions."));
        connect(remove, &QToolButton::clicked, this, [this, a] { emit adjustmentRemoveRequested(a); });
        h->addWidget(remove);

        m_adjustmentsLayout->addWidget(row);
    }
}

void StripStatePanel::showPage(const QString& inputUid, const QString& label, QSize sizeInStrip,
                               bool excluded, bool stripGraded)
{
    m_pageUid = inputUid;
    m_subject->setText(label);
    m_details->setText(tr("%1 × %2 px in the strip").arg(sizeInStrip.width()).arg(sizeInStrip.height()));

    m_populating = true;
    {
        const QSignalBlocker block(m_excluded);
        m_excluded->setChecked(excluded);
    }
    m_populating = false;

    m_adjustmentsHeading->setVisible(false);
    m_adjustments->setVisible(false);
    m_excluded->setVisible(true);

    // Says what the checkbox does *now*, so that ticking it on an ungraded strip is not mistaken for a
    // control that does nothing.
    m_note->setText(!stripGraded ? tr("The strip has no colour correction yet; this takes effect once it has.")
                    : excluded   ? tr("Renders in its own colours.")
                                 : tr("Takes the strip's colour correction."));
    m_note->setVisible(true);
}

}  // namespace StripEdit
