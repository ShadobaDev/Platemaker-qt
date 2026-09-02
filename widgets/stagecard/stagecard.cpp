#include "stagecard.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>

#include <algorithm>

StageCard::StageCard(QWidget* parent)
    : QWidget(parent)
{
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
    m_addButton = makeBtn(QStringLiteral("+"));
    m_addButton->setObjectName(QStringLiteral("stageAddBtn")); // targeted by the :hover rule (green reveal)
    m_editButton   = makeBtn(tr("Edit"));
    m_removeButton = makeBtn(QStringLiteral("−")); // U+2212 minus
    m_addButton->setToolTip(tr("Add this step"));
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
    const bool dim       = isDimmed();
    const bool activeOpt = (m_kind == Kind::Optional && m_active);
    const QString bg          = dim ? QStringLiteral("palette(window)") : QStringLiteral("palette(base)");
    const QString borderStyle = dim ? QStringLiteral("dashed") : QStringLiteral("solid");
    const QString borderColor = activeOpt ? QStringLiteral("palette(highlight)") : QStringLiteral("palette(mid)");

    // The whole frame lives in the style sheet so Qt tracks :hover for us (correctly, incl. over the child
    // buttons and in both directions — the manual enter/leave tracking could not). Frame colours are
    // palette() references (theme-aware); the "+" is transparent until the card is hovered, then green —
    // a hover-reveal driven entirely by :hover, so it can never get "stuck".
    setStyleSheet(QStringLiteral(
        "StageCard { background: %1; border: 1px %2 %3; border-radius: 9px; }"
        "StageCard:hover { border: 2px %2 palette(highlight); }"
        "StageCard QToolButton { border: none; background: transparent; padding: 1px 4px; }"
        "StageCard QToolButton#stageAddBtn { color: transparent; font-weight: bold; }"
        "StageCard:hover QToolButton#stageAddBtn { color: rgb(46, 160, 67); }") // success green
        .arg(bg, borderStyle, borderColor));

    m_titleLabel->setEnabled(!dim);          // greyed title on an inactive placeholder
    m_addButton->setVisible(m_wantAdd);      // space reserved; the QSS reveals its colour on hover
    m_editButton->setVisible(m_wantEdit);
    m_removeButton->setVisible(m_wantRemove);
    updateGeometry();                        // sizeHint depends on dim
    update();
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
    // A custom QWidget only honours style-sheet background/border/radius if it paints the style's box.
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
