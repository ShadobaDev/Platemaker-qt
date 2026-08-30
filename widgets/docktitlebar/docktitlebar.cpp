#include "docktitlebar.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScreen>
#include <QSize>
#include <QStyle>
#include <QToolButton>

DockTitleBar::DockTitleBar(QDockWidget *dock, QWidget *parent)
    : QWidget(parent)
    , m_dock(dock)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 2, 2);
    layout->setSpacing(23);          // roomy gaps between the buttons, like the native title-bar controls

    auto *titleLabel = new QLabel(m_dock->windowTitle(), this);
    connect(m_dock, &QDockWidget::windowTitleChanged, titleLabel, &QLabel::setText);
    layout->addWidget(titleLabel, 1);

    const auto addButton = [&](QStyle::StandardPixmap icon, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setIcon(style()->standardIcon(icon));
        b->setIconSize(QSize(15, 15));   // small glyphs, matching the native min/max/close buttons
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b);
        return b;
    };

    // Minimise → the owner decides dock ⇄ detach. The style's minimise glyph is a short dash; swap in a
    // longer one so it reads like the native control (see dashIcon).
    QToolButton *minButton = addButton(QStyle::SP_TitleBarMinButton, tr("Dock ⇄ detach"));
    minButton->setIcon(dashIcon(minButton->iconSize().height()));
    connect(minButton, &QToolButton::clicked, this, &DockTitleBar::minimiseClicked);

    // Maximise ⇄ restore is identical for every dock, so handle it here.
    connect(addButton(QStyle::SP_TitleBarMaxButton, tr("Maximise to the full screen")),
            &QToolButton::clicked, this, &DockTitleBar::toggleMaximise);

    // Close → the owner decides hide vs destroy.
    connect(addButton(QStyle::SP_TitleBarCloseButton, tr("Close")),
            &QToolButton::clicked, this, &DockTitleBar::closeClicked);

    // The maximise toggle is only meaningful within one float state; reset it whenever that changes
    // (docking, detaching), so the next maximise fills the screen instead of restoring a stale geometry.
    connect(m_dock, &QDockWidget::topLevelChanged, this, [this](bool) { m_maximised = false; });
}

void DockTitleBar::toggleMaximise()
{
    if (!m_dock->isFloating())
        m_dock->setFloating(true);   // fires topLevelChanged → m_maximised reset to false
    if (m_maximised) {
        m_dock->setGeometry(m_restoreGeom);
        m_maximised = false;
    } else {
        m_restoreGeom = m_dock->geometry();
        if (const QScreen *scr = m_dock->screen())
            m_dock->setGeometry(scr->availableGeometry());
        m_maximised = true;
    }
}

QIcon DockTitleBar::dashIcon(int px) const
{
    // Draw the minimise dash a bit longer than the style's default, in the same stroke colour, at the
    // device pixel ratio so it stays crisp on HiDPI.
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qRound(px * dpr), qRound(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(palette().color(QPalette::WindowText));
    pen.setWidth(1);
    p.setPen(pen);
    const int     y      = px / 2;
    const int     margin = qMax(2, px / 7);   // 2px min, ~1/7 of the icon size
    constexpr int offset = 3;                 // nudge right so it sits under the button like the glyphs
    p.drawLine(margin + offset, y, px - margin + offset, y);
    return QIcon(pm);
}
