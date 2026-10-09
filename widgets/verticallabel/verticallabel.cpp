#include "verticallabel.hpp"

#include <QFontMetricsF>
#include <QtMath>
#include <QPainter>
#include <QStyle>

namespace {
constexpr int k_margin = 2;   //!< Padding around the text, on every side.
}

VerticalLabel::VerticalLabel(QWidget *parent)
    : QWidget(parent)
{
    // As long as its text when there is room, shorter (elided) when there is not.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void VerticalLabel::setText(const QString &text)
{
    if (text == m_text) return;
    m_text = text;
    setToolTip(text);
    updateGeometry();
    update();
}

QSize VerticalLabel::sizeHint() const
{
    // Turned on its side: the line height is the width, the text length is the height.
    // Rounded up from the fractional advance elidedText() measures with, or a text given exactly its own
    // length would still elide.
    const QFontMetricsF fm(font());
    return {qCeil(fm.height()) + 2 * k_margin, qCeil(fm.horizontalAdvance(m_text)) + 2 * k_margin};
}

QSize VerticalLabel::minimumSizeHint() const
{
    // Any height will do — the text elides.
    return {sizeHint().width(), 0};
}

void VerticalLabel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // Rotate about the top-right corner, so the text runs from the top downwards.
    p.translate(width(), 0);
    p.rotate(90);
    const QRect   r(k_margin, k_margin, height() - 2 * k_margin, width() - 2 * k_margin);
    const QString shown = fontMetrics().elidedText(m_text, Qt::ElideRight, r.width());
    style()->drawItemText(&p, r, Qt::AlignLeft | Qt::AlignVCenter, palette(), isEnabled(), shown,
                          QPalette::WindowText);
}
