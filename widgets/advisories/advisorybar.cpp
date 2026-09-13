#include "advisorybar.h"

#include "advisories.h"
#include "badge.h"

#include <QHBoxLayout>
#include <QLayoutItem>

namespace {

constexpr int k_spacing = 6;   //!< Between chips. The gap is layout, never a drawn separator.
constexpr int k_margin  = 4;   //!< Around the run, so a bar along a window edge is not flush with it.

[[nodiscard]] BadgeTone toneFor(Advisory::Level level)
{
    switch (level) {
    case Advisory::Level::Error:   return BadgeTone::Error;
    case Advisory::Level::Warning: return BadgeTone::Warning;
    case Advisory::Level::Info:    break;
    }
    return BadgeTone::Info;
}

} // namespace

AdvisoryBar::AdvisoryBar(Advisories* registry, QWidget* parent)
    : QWidget(parent)
    , m_advisories(registry)
{
    m_chips = new QHBoxLayout(this);
    m_chips->setContentsMargins(k_margin, k_margin, k_margin, k_margin);
    m_chips->setSpacing(k_spacing);
    // The bar is exactly as wide as its chips and no wider, so **where it sits is the host's to say** —
    // a status bar pins it right by itself, a layout is told to. Maximum rather than Fixed so a narrow
    // window squeezes it instead of being forced open by it.
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    if (m_advisories)
        connect(m_advisories, &Advisories::changed, this, &AdvisoryBar::rebuild);
    rebuild();
}

void AdvisoryBar::setProjectUid(const QString& projectUid)
{
    if (m_projectUid == projectUid)
        return;
    m_projectUid = projectUid;
    rebuild();
}

void AdvisoryBar::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    updateVisibility();
}

void AdvisoryBar::rebuild()
{
    while (QLayoutItem* item = m_chips->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QList<Advisory> standing =
        m_advisories ? m_advisories->forProject(m_projectUid) : QList<Advisory>{};

    for (const Advisory& a : standing) {
        // The way out of the problem is part of what the chip says, so it is part of the sentence
        // behind it too — a click with no hint that it does anything is not an offer.
        QString tip = a.detail;
        if (!a.actionText.isEmpty() && a.action)
            tip += QStringLiteral("\n\n") + tr("Click to %1.").arg(a.actionText.toLower());

        m_chips->addWidget(makeBadge(toneBadge(toneFor(a.level), a.text, tip, palette()),
                                     this, a.action));
    }
    m_hasAny = !standing.isEmpty();
    // An empty bar takes no room at all, not even its margins. Hiding it would be enough in a plain
    // layout, but a QStatusBar re-shows the permanent widgets it holds whenever it reformats, and a
    // surface that can be shown behind our back had better be zero-sized when it has nothing to say.
    const int m = m_hasAny ? k_margin : 0;
    m_chips->setContentsMargins(m, m, m, m);
    updateVisibility();
}

void AdvisoryBar::updateVisibility()
{
    // Nothing to say means no bar at all. A permanently visible empty strip reports that all is well,
    // which nobody asked and which costs a row of pixels in every window that hosts one.
    setVisible(m_active && m_hasAny);
}
