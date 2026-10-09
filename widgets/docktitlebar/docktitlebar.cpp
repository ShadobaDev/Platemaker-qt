#include "docktitlebar.hpp"

#include <QDockWidget>
#include <QBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QScreen>
#include <QSize>
#include <QSpacerItem>
#include <QStyle>
#include <QToolButton>

#include <utility>

namespace {
// Margins of the bar, left/top/right/bottom: a little room before the title while it is a row; an even
// border around the stacked buttons while it is a collapsed column.
constexpr int k_rowMarginLeft  = 6;
constexpr int k_rowMarginOther = 2;
constexpr int k_columnMargin   = 2;
constexpr int k_columnGap      = 4;   // between the stacked buttons — the row's roomy gaps would waste the column
}

DockTitleBar::DockTitleBar(QDockWidget *dock, QWidget *parent)
    : QWidget(parent)
    , m_dock(dock)
{
    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(k_rowMarginLeft, k_rowMarginOther, k_rowMarginOther, k_rowMarginOther);
    m_layout->setSpacing(0);         // gaps added explicitly below, so the minimise button can be nudged

    m_title = new QLabel(m_dock->windowTitle(), this);
    connect(m_dock, &QDockWidget::windowTitleChanged, m_title, &QLabel::setText);
    m_layout->addWidget(m_title, 1);

    const auto addButton = [&](QStyle::StandardPixmap icon, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setIcon(style()->standardIcon(icon));
        b->setIconSize(QSize(15, 15));   // small glyphs, matching the native min/max/close buttons
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        m_layout->addWidget(b);
        return b;
    };
    const auto addGap = [&](int length) {
        auto *item = new QSpacerItem(length, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        m_layout->addSpacerItem(item);
        m_gaps.append({item, length});
    };

    constexpr int gap   = 23;   // roomy gaps between the buttons, like the native title-bar controls
    constexpr int nudge = 3;    // shift the minimise button right so its hover sits under the centred dash
    constexpr int offset = 5;   // shift all buttons left there is addtional sapce on the right (to match the native windows title bar layout)

    // Minimise → the owner decides dock ⇄ detach. The dash is drawn centred (see dashIcon); the button is
    // nudged right by `nudge` and the following gap trimmed by the same, so the dash keeps its position
    // and the hover sits symmetrically around it — without moving maximise / close.
    addGap(gap + nudge);
    QToolButton *minButton = addButton(QStyle::SP_TitleBarMinButton, tr("Dock ⇄ detach"));
    minButton->setIcon(dashIcon(minButton->iconSize().height()));
    connect(minButton, &QToolButton::clicked, this, &DockTitleBar::minimiseClicked);

    // Maximise ⇄ restore is identical for every dock, so handle it here.
    addGap(gap - nudge);
    connect(addButton(QStyle::SP_TitleBarMaxButton, tr("Maximise to the full screen")),
            &QToolButton::clicked, this, &DockTitleBar::toggleMaximise);

    // Close → the owner decides hide vs destroy.
    addGap(gap);
    connect(addButton(QStyle::SP_TitleBarCloseButton, tr("Close")),
            &QToolButton::clicked, this, &DockTitleBar::closeClicked);
    addGap(offset);

    // The maximise toggle is only meaningful within one float state; reset it whenever that changes
    // (docking, detaching), so the next maximise fills the screen instead of restoring a stale geometry.
    // Collapsing is docked-only: a floating dock shows expanded and hides the collapse button, but the
    // choice is kept, so docking it again folds it back.
    connect(m_dock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        m_maximised = false;
        if (!m_collapseButton) return;
        m_collapseButton->setVisible(!floating);
        applyCollapsed();
        if (m_collapsed) emit collapseToggled(!floating);
    });
}

void DockTitleBar::enableCollapse()
{
    if (m_collapseButton) return;
    m_collapseButton = new QToolButton(this);
    m_collapseButton->setIconSize(QSize(15, 15));
    m_collapseButton->setAutoRaise(true);
    m_collapseButton->setFocusPolicy(Qt::NoFocus);
    m_collapseButton->setVisible(!m_dock->isFloating());
    // First in the row: right after the title, before the minimise button's gap.
    m_layout->insertWidget(m_layout->indexOf(m_title) + 1, m_collapseButton);
    connect(m_collapseButton, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    applyCollapsed();
}

void DockTitleBar::setCollapsed(bool collapsed)
{
    if (!m_collapseButton || collapsed == m_collapsed) return;
    m_collapsed = collapsed;
    applyCollapsed();
    if (!m_dock->isFloating()) emit collapseToggled(collapsed);
}

void DockTitleBar::applyCollapsed()
{
    const bool collapsed = m_collapsed && !m_dock->isFloating();
    m_collapseButton->setIcon(collapseIcon(m_collapseButton->iconSize().height(), collapsed));
    m_collapseButton->setToolTip(collapsed ? tr("Expand panel") : tr("Collapse panel"));

    // A row with the title and roomy gaps, or a column of the buttons alone with short gaps between them.
    m_title->setVisible(!collapsed);
    m_layout->setDirection(collapsed ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (collapsed)
        m_layout->setContentsMargins(k_columnMargin, k_columnMargin, k_columnMargin, k_columnMargin);
    else
        m_layout->setContentsMargins(k_rowMarginLeft, k_rowMarginOther, k_rowMarginOther, k_rowMarginOther);
    for (const Gap &g : std::as_const(m_gaps)) {
        if (collapsed) g.item->changeSize(0, k_columnGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
        else           g.item->changeSize(g.length, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    }
    m_layout->invalidate();
    updateGeometry();
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
    const int y      = px / 2;
    const int margin = qMax(2, px / 7) + 1;   // 2px min, ~1/7 of the icon size, plus 1px to match windows native min button dash size
    p.drawLine(margin, y, px - margin, y);   // centred; the min button itself is nudged in the layout
    return QIcon(pm);
}

QIcon DockTitleBar::collapseIcon(int px, bool pointLeft) const
{
    // A chevron against a bar at the right edge, in the same stroke colour as the other glyphs and at the
    // device pixel ratio. The bar stays put; only the chevron turns, so the button reads as one toggle.
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qRound(px * dpr), qRound(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(palette().color(QPalette::WindowText));
    pen.setWidthF(1.0);
    p.setPen(pen);
    const qreal m     = qMax(2, px / 7) + 1;   // the same margin as the minimise dash
    const qreal bar   = px - m;
    const qreal tip   = bar - m;               // the chevron's point stops short of the bar
    const qreal back  = m + 1;
    const qreal mid   = px / 2.0;
    const qreal point = pointLeft ? back : tip;   // the chevron's point
    const qreal open  = pointLeft ? tip : back;   // its open end
    p.drawLine(QPointF(open, m), QPointF(point, mid));
    p.drawLine(QPointF(point, mid), QPointF(open, px - m));
    p.drawLine(QPointF(bar, m), QPointF(bar, px - m));
    return QIcon(pm);
}
