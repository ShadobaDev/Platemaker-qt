#include "badge.h"

#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QWidget>

namespace {

constexpr int k_hPad     = 7;   //!< Horizontal padding inside a chip.
constexpr int k_vPad     = 2;   //!< Added to the font height for the chip's own height.
constexpr int k_radius   = 5;   //!< Corner radius.
constexpr int k_gap      = 8;   //!< Space before each chip, so chips never touch.
constexpr int k_darkStep = 150; //!< How much darker the derived border is than the fill.

//! Slightly smaller and bold, robust to point- vs pixel-sized base fonts.
[[nodiscard]] QFont badgeFontFor(const QFont& base)
{
    QFont f = base;
    f.setBold(true);
    if (base.pointSizeF() > 0)
        f.setPointSizeF(base.pointSizeF() * 0.85);
    else if (base.pixelSize() > 0)
        f.setPixelSize(qMax(1, static_cast<int>(base.pixelSize() * 0.85)));
    return f;
}

//! Black or white, whichever can be read on @p fill. Qt's own lightness is HSL, which calls a saturated
//! yellow and a saturated blue equally light; perceived luminance does not, and a label has to be read
//! rather than measured.
[[nodiscard]] QColor readableOn(const QColor& fill)
{
    const qreal luma = 0.299 * fill.redF() + 0.587 * fill.greenF() + 0.114 * fill.blueF();
    return luma > 0.55 ? QColor(0x11, 0x11, 0x11) : QColor(0xF5, 0xF5, 0xF5);
}

/**
 * @brief The hue for a tone, set to a lightness that reads against @p palette's Base.
 *
 * The hue carries the meaning — red is wrong, amber is worth knowing, blue is a remark — and only the
 * lightness follows the theme. Choosing a whole second palette for dark mode would mean maintaining two
 * sets of decisions where one decision plus a rule does the job.
 */
[[nodiscard]] QColor tonedFill(const QColor& hue, const QPalette& palette)
{
    const bool darkUi = palette.color(QPalette::Base).lightnessF() < 0.5;
    QColor c = hue;
    c.setHslF(c.hueF(), c.hslSaturationF(), darkUi ? 0.34 : 0.76);
    return c;
}

//! A chip as a widget, for hosts that cannot paint one themselves (a status bar, a layout).
class BadgeWidget : public QWidget
{
public:
    BadgeWidget(const Badge& badge, QWidget* parent, std::function<void()> onClick)
        : QWidget(parent)
        , m_badge(badge)
        , m_onClick(std::move(onClick))
    {
        setToolTip(badge.detail);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (m_onClick)
            setCursor(Qt::PointingHandCursor);
    }

    [[nodiscard]] QSize sizeHint() const override { return badgeSize(m_badge, font()); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        paintBadge(p, rect(), m_badge, font());
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        // On release inside, as a button behaves: a press the user drags off the chip is a press they
        // changed their mind about.
        if (m_onClick && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
            m_onClick();
        QWidget::mouseReleaseEvent(event);
    }

private:
    Badge                 m_badge;
    std::function<void()> m_onClick;
};

} // namespace

Badge toneBadge(BadgeTone tone, const QString& text, const QString& detail, const QPalette& palette)
{
    // The four hues, and nothing else in this application names one.
    QColor hue;
    switch (tone) {
    case BadgeTone::Info:    hue = QColor(0x3B, 0x82, 0xF6); break;   // blue — a remark
    case BadgeTone::Warning: hue = QColor(0xE0, 0x87, 0x2C); break;   // amber — worth knowing
    case BadgeTone::Error:   hue = QColor(0xD2, 0x3F, 0x3F); break;   // red — this will not do what you meant
    case BadgeTone::Neutral:
        // Reporting rather than advising, so it takes the window's own colour and claims no severity.
        return Badge{text, palette.color(QPalette::Button), detail, {}, {}, {}};
    }
    return Badge{text, tonedFill(hue, palette), detail, {}, {}, {}};
}

QSize badgeSize(const Badge& badge, const QFont& base)
{
    const QFontMetrics fm(badgeFontFor(base));
    return {fm.horizontalAdvance(badge.text) + 2 * k_hPad, fm.height() + k_vPad};
}

void paintBadge(QPainter& painter, const QRect& chip, const Badge& badge, const QFont& base)
{
    const QColor border = badge.border.isValid() ? badge.border : badge.fill.darker(k_darkStep);
    const QColor label  = badge.textColour.isValid() ? badge.textColour : readableOn(badge.fill);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);   // a rounded chip without it is a brick
    painter.setPen(QPen(border, 1));
    if (badge.fill2.isValid()) {
        QLinearGradient g(chip.topLeft(), chip.bottomLeft());
        g.setColorAt(0.0, badge.fill);
        g.setColorAt(1.0, badge.fill2);
        painter.setBrush(g);
    } else {
        painter.setBrush(badge.fill);
    }
    // Inset by half the pen, which straddles the path — otherwise the border is clipped by the rect.
    painter.drawRoundedRect(QRectF(chip).adjusted(0.5, 0.5, -0.5, -0.5), k_radius, k_radius);

    painter.setFont(badgeFontFor(base));
    painter.setPen(label);
    painter.drawText(chip, Qt::AlignCenter, badge.text);
    painter.restore();
}

QList<QRect> layOutBadges(QPainter* painter, const QFont& base, const QList<Badge>& badges,
                          int left, int top, int lineHeight, int right)
{
    QList<QRect> rects;
    int x = left;
    for (const Badge& b : badges) {
        const QSize size = badgeSize(b, base);
        x += k_gap;
        if (x + size.width() > right)
            break;   // and everything after it: a chip half off the row says less than no chip
        const QRect chip(x, top + (lineHeight - size.height()) / 2, size.width(), size.height());
        if (painter)
            paintBadge(*painter, chip, b, base);
        rects.append(chip);
        x += size.width();
    }
    return rects;
}

QWidget* makeBadge(const Badge& badge, QWidget* parent, std::function<void()> onClick)
{
    return new BadgeWidget(badge, parent, std::move(onClick));
}
