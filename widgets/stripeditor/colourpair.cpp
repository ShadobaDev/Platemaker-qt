#include "colourpair.h"

#include <utility>

#include <QColorDialog>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QSettings>
#include <QToolButton>

namespace StripEdit {

namespace {

// The panel and what sits in it. The two swatches overlap by a third, which is what makes them read as
// one control with a front and a back rather than as two buttons.
constexpr int k_panelW   = 66;
constexpr int k_panelH   = 50;
constexpr int k_swatch   = 26;   //!< A swatch button; its icon is inset by the frame the icon draws.
constexpr int k_iconPx   = 22;
constexpr int k_smallBtn = 15;   //!< Swap and reset.

const auto k_primaryKey   = QLatin1String("StripEditor/primaryColour");
const auto k_secondaryKey = QLatin1String("StripEditor/secondaryColour");

/**
 * @brief A rounded swatch of @p colour, framed in the palette's text colour.
 *
 * The frame is what makes a white swatch visible on a light theme and a black one on a dark theme, and
 * the chequer behind it is what tells a transparent colour from a pale one — a flat swatch of a 10%
 * alpha fill looks like an opaque near-white.
 */
[[nodiscard]] QPixmap swatch(const QColor& colour, const QPalette& pal, qreal dpr)
{
    QPixmap px(QSize(k_iconPx, k_iconPx) * dpr);
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box(0.5, 0.5, k_iconPx - 1, k_iconPx - 1);
    QPainterPath rounded;
    rounded.addRoundedRect(box, 3, 3);
    p.setClipPath(rounded);

    if (colour.alpha() < 255) {
        const qreal half = k_iconPx / 2.0;
        p.fillRect(box, pal.color(QPalette::Base));
        p.fillRect(QRectF(box.left(), box.top(), half, half), pal.color(QPalette::AlternateBase));
        p.fillRect(QRectF(box.left() + half, box.top() + half, half, half),
                   pal.color(QPalette::AlternateBase));
    }
    p.fillPath(rounded, colour);

    p.setClipping(false);
    QColor frame = pal.color(QPalette::Text);
    frame.setAlpha(160);
    p.setPen(frame);
    p.setBrush(Qt::NoBrush);
    p.drawPath(rounded);
    return px;
}

//! Two arrows, drawn rather than typed: a 15px button clips a text glyph at some font sizes.
[[nodiscard]] QPixmap swapIcon(const QPalette& pal, qreal dpr)
{
    constexpr int side = 12;
    QPixmap px(QSize(side, side) * dpr);
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(pal.color(QPalette::Text));
    pen.setWidthF(1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    p.drawLine(QPointF(1.5, 4), QPointF(10.5, 4));      // out along the top
    p.drawPolyline(QPolygonF({{8, 1.8}, {10.5, 4}, {8, 6.2}}));
    p.drawLine(QPointF(10.5, 8), QPointF(1.5, 8));      // and back along the bottom
    p.drawPolyline(QPolygonF({{4, 5.8}, {1.5, 8}, {4, 10.2}}));
    return px;
}

//! The reset target, drawn as what it restores: a black swatch in front of a white one.
[[nodiscard]] QPixmap resetIcon(const QPalette& pal, qreal dpr)
{
    constexpr int side = 12;
    QPixmap px(QSize(side, side) * dpr);
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);

    QPainter p(&px);
    QColor frame = pal.color(QPalette::Text);
    frame.setAlpha(160);
    p.setPen(frame);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(4.5, 4.5, 6.5, 6.5));
    p.setBrush(Qt::black);
    p.drawRect(QRectF(0.5, 0.5, 6.5, 6.5));
    return px;
}

}  // namespace

ColourPair::ColourPair(QWidget* parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);   // a panel of its own: furniture, not another tool tile
    setFixedSize(k_panelW, k_panelH);

    QSettings s;
    m_primary   = QColor(s.value(k_primaryKey,   QColor(Qt::black).name(QColor::HexArgb)).toString());
    m_secondary = QColor(s.value(k_secondaryKey, QColor(Qt::white).name(QColor::HexArgb)).toString());
    if (!m_primary.isValid())   m_primary   = Qt::black;
    if (!m_secondary.isValid()) m_secondary = Qt::white;

    // Placed by hand rather than by a layout, because the overlap *is* the design and no layout
    // expresses one.
    const auto addSwatch = [this](bool secondary, QPoint at) {
        auto* b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setIconSize(QSize(k_iconPx, k_iconPx));
        b->setGeometry(at.x(), at.y(), k_swatch, k_swatch);
        connect(b, &QToolButton::clicked, this, [this, secondary] { pick(secondary); });
        return b;
    };
    m_secondaryButton = addSwatch(true,  {25, 18});
    m_primaryButton   = addSwatch(false, {6, 5});
    m_primaryButton->raise();   // the primary is the one in front, in every application that has a pair

    m_swapButton = new QToolButton(this);
    m_swapButton->setAutoRaise(true);
    m_swapButton->setIconSize(QSize(12, 12));
    m_swapButton->setGeometry(k_panelW - k_smallBtn - 4, 3, k_smallBtn, k_smallBtn);
    connect(m_swapButton, &QToolButton::clicked, this, [this] {
        std::swap(m_primary, m_secondary);
        store();
        refresh();
        emit changed();
    });

    m_resetButton = new QToolButton(this);
    m_resetButton->setAutoRaise(true);
    m_resetButton->setIconSize(QSize(12, 12));
    m_resetButton->setGeometry(3, k_panelH - k_smallBtn - 4, k_smallBtn, k_smallBtn);
    connect(m_resetButton, &QToolButton::clicked, this, [this] {
        if (m_primary == QColor(Qt::black) && m_secondary == QColor(Qt::white))
            return;
        m_primary   = Qt::black;
        m_secondary = Qt::white;
        store();
        refresh();
        emit changed();
    });

    refresh();
}

void ColourPair::set(const QColor& colour, bool secondary)
{
    if (!colour.isValid())
        return;
    QColor& slot = secondary ? m_secondary : m_primary;
    if (slot == colour)
        return;
    slot = colour;

    store();
    refresh();
    emit changed();
}

void ColourPair::changeEvent(QEvent* e)
{
    QFrame::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ThemeChange)
        refresh();   // the swatch frames and the chequer are drawn in the palette's colours
}

void ColourPair::pick(bool secondary)
{
    const QColor chosen = QColorDialog::getColor(
        secondary ? m_secondary : m_primary, this,
        secondary ? tr("Secondary colour") : tr("Primary colour"), QColorDialog::ShowAlphaChannel);
    set(chosen, secondary);
}

void ColourPair::store()
{
    QSettings s;
    s.setValue(k_primaryKey,   m_primary.name(QColor::HexArgb));
    s.setValue(k_secondaryKey, m_secondary.name(QColor::HexArgb));
}

void ColourPair::refresh()
{
    const qreal dpr = devicePixelRatioF();
    m_primaryButton->setIcon(swatch(m_primary, palette(), dpr));
    m_secondaryButton->setIcon(swatch(m_secondary, palette(), dpr));
    m_swapButton->setIcon(swapIcon(palette(), dpr));
    m_resetButton->setIcon(resetIcon(palette(), dpr));

    m_primaryButton->setToolTip(tr("Primary colour — %1.\nClick to choose it; the eyedropper fills it.")
                                    .arg(m_primary.name(QColor::HexArgb)));
    m_secondaryButton->setToolTip(tr("Secondary colour — %1.\nThe eyedropper fills it with Ctrl held.")
                                      .arg(m_secondary.name(QColor::HexArgb)));
    m_swapButton->setToolTip(tr("Swap the two colours"));
    m_resetButton->setToolTip(tr("Reset to black and white"));
}

}  // namespace StripEdit
