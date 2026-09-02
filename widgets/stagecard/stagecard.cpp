#include "stagecard.h"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <algorithm>

StageCard::StageCard(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);

    m_titleLabel = new QLabel(this);
    { QFont f = m_titleLabel->font(); f.setBold(true); m_titleLabel->setFont(f); }

    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setEnabled(false); // muted (disabled-palette text)
    { QFont f = m_subtitleLabel->font(); f.setPointSizeF(std::max(6.5, f.pointSizeF() - 1.0));
      m_subtitleLabel->setFont(f); }

    auto makeBtn = [this](const QString& text) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->hide();
        return b;
    };
    m_addButton    = makeBtn(QStringLiteral("+"));
    m_editButton   = makeBtn(tr("Edit"));
    m_removeButton = makeBtn(QStringLiteral("−")); // U+2212 minus

    // The "+" is the one deliberate colour accent (green "add"), applied via the button's own palette so
    // it still tracks the theme's background — no stylesheet.
    { QPalette p = m_addButton->palette();
      p.setColor(QPalette::ButtonText, QColor(46, 160, 67)); // success green (reads on light & dark)
      m_addButton->setPalette(p);
      QFont f = m_addButton->font(); f.setBold(true); f.setPointSizeF(f.pointSizeF() + 2.0);
      m_addButton->setFont(f);
      m_addButton->setToolTip(tr("Add this step")); }
    m_editButton->setToolTip(tr("Open the editor"));
    m_removeButton->setToolTip(tr("Remove this step"));

    connect(m_addButton,    &QToolButton::clicked, this, &StageCard::addRequested);
    connect(m_editButton,   &QToolButton::clicked, this, &StageCard::editRequested);
    connect(m_removeButton, &QToolButton::clicked, this, &StageCard::removeRequested);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 8, 10, 8);
    lay->setSpacing(6);
    lay->addWidget(m_titleLabel);
    lay->addStretch(1);
    lay->addWidget(m_subtitleLabel);
    lay->addWidget(m_addButton);
    lay->addWidget(m_editButton);
    lay->addWidget(m_removeButton);

    // Hover over a child (button/label) makes the card's own leaveEvent fire, and leaving that child never
    // sends the card another event — so watch the children too and recompute hover from the real cursor.
    const QList<QWidget*> kids{ m_titleLabel, m_subtitleLabel, m_addButton, m_editButton, m_removeButton };
    for (QWidget* kid : kids)
        kid->installEventFilter(this);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    restyle();
}

QSize StageCard::sizeHint() const
{
    return isDimmed() ? QSize(320, 48) : QSize(360, 56);
}

void StageCard::setTitle(const QString& title)
{
    m_titleLabel->setText(title);
}

void StageCard::setSubtitle(const QString& subtitle)
{
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

void StageCard::setKind(Kind kind)
{
    if (m_kind == kind) return;
    m_kind = kind;
    restyle();
}

void StageCard::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    restyle();
}

void StageCard::setActions(bool add, bool edit, bool remove)
{
    m_wantAdd = add; m_wantEdit = edit; m_wantRemove = remove;
    restyle();
}

void StageCard::restyle()
{
    const bool dim = isDimmed();
    m_titleLabel->setEnabled(!dim);                   // greyed title when it's an inactive placeholder
    m_addButton->setVisible(m_wantAdd && m_hovered);  // the "+" is revealed on hover
    m_editButton->setVisible(m_wantEdit);
    m_removeButton->setVisible(m_wantRemove);
    updateGeometry();                                 // sizeHint depends on dim
    update();
}

void StageCard::enterEvent(QEnterEvent*) { updateHover(); }
void StageCard::leaveEvent(QEvent*)      { updateHover(); }

bool StageCard::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        updateHover();
    return QWidget::eventFilter(watched, event);
}

void StageCard::updateHover()
{
    // Hover = the widget directly under the cursor is this card or one of its children. Hit-testing the
    // real cursor (not geometric rect containment) avoids the top-edge boundary bug and the "left via a
    // child" case that left borders stuck highlighted.
    const QWidget* under = QApplication::widgetAt(QCursor::pos());
    const bool hovered = under && (under == this || isAncestorOf(under));
    if (hovered == m_hovered)
        return;
    m_hovered = hovered;
    m_addButton->setVisible(m_wantAdd && m_hovered); // "+" revealed on hover
    update();                                        // repaint the border
}

void StageCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void StageCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit editRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void StageCard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const bool dim = isDimmed();
    const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);

    // Fill: recessed for a placeholder, a card surface otherwise.
    const QColor fill = dim ? pal.color(QPalette::Window) : pal.color(QPalette::Base);

    // Border: highlighted on hover; also highlighted (subtly) for an *active* optional step so an enabled
    // effect stands out; dashed for the empty placeholder.
    QColor border;
    if (m_hovered)                                   border = pal.color(QPalette::Highlight);
    else if (m_kind == Kind::Optional && m_active)   border = pal.color(QPalette::Highlight);
    else                                             border = pal.color(QPalette::Mid);
    QPen pen(border);
    pen.setWidthF(m_hovered ? 2.0 : 1.2);
    if (dim)
        pen.setStyle(Qt::DashLine);

    p.setPen(pen);
    p.setBrush(fill);
    p.drawRoundedRect(card, 9, 9);
}
