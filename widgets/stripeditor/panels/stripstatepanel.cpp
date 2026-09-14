#include "stripstatepanel.h"

#include <QCheckBox>
#include <QLabel>
#include <QLocale>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

namespace StripEdit {

namespace {

constexpr int k_valueDecimals = 2;   //!< Enough to tell 1.05 from 1.00, which is where a grade is visible.

[[nodiscard]] QString number(double v)
{
    return QLocale().toString(v, 'f', k_valueDecimals);
}

/**
 * @brief The adjustments a grade applies, one line each, named as GIMP's *Colours* menu names them.
 *
 * An adjustment is *applied* when any of its fields is off its neutral value — the same rule
 * `Models::isNeutral()` applies to the whole grade, broken down so the artist can see which part is doing
 * something. The lines follow the order the library runs them in, which is also the order that decides
 * the result.
 */
[[nodiscard]] QStringList appliedAdjustments(const Platemaker::Models::ColourCorrection& cc)
{
    QStringList lines;
    if (Platemaker::Models::hasAnyCurve(cc.curves))
        lines << QObject::tr("Curves");
    if (cc.brightness != 0.0 || cc.contrast != 1.0)
        lines << QObject::tr("Brightness & contrast — brightness %1, contrast %2")
                     .arg(number(cc.brightness), number(cc.contrast));
    if (cc.saturation != 1.0)
        lines << QObject::tr("Saturation — %1").arg(number(cc.saturation));
    return lines;
}

}  // namespace

StripStatePanel::StripStatePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);

    // Named the way ObjectStatePanel names its subject, so the two read as one surface.
    m_subject = new QLabel(this);
    m_subject->setTextFormat(Qt::PlainText);
    QFont subjectFont = m_subject->font();
    subjectFont.setBold(true);
    m_subject->setFont(subjectFont);
    lay->addWidget(m_subject);

    m_details = new QLabel(this);
    m_details->setTextFormat(Qt::PlainText);
    m_details->setWordWrap(true);
    lay->addWidget(m_details);

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

    QStringList lines;
    lines << (excludedCount > 0
                  ? tr("%n page(s)", "", pageCount) + QStringLiteral(" · ")
                        + tr("%n excluded from colour correction", "", excludedCount)
                  : tr("%n page(s)", "", pageCount));
    lines << QString();
    lines << tr("Colour correction");
    const QStringList applied = appliedAdjustments(cc);
    lines << (applied.isEmpty() ? tr("None — every page renders in its own colours.")
                                : applied.join(QLatin1Char('\n')));
    m_details->setText(lines.join(QLatin1Char('\n')));

    m_excluded->setVisible(false);
    m_note->setVisible(false);
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
    m_excluded->setVisible(true);

    // Says what the checkbox does *now*, so that ticking it on an ungraded strip is not mistaken for a
    // control that does nothing.
    m_note->setText(!stripGraded ? tr("The strip has no colour correction yet; this takes effect once it has.")
                    : excluded   ? tr("Renders in its own colours.")
                                 : tr("Takes the strip's colour correction."));
    m_note->setVisible(true);
}

}  // namespace StripEdit
