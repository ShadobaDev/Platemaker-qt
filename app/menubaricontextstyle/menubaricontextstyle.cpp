#include "menubaricontextstyle.hpp"

#include <QIcon>
#include <QPainter>
#include <QStyleOptionMenuItem>

QSize MenuBarIconTextStyle::sizeFromContents(ContentsType type, const QStyleOption *opt,
                                             const QSize &size, const QWidget *widget) const
{
    QSize s = QProxyStyle::sizeFromContents(type, opt, size, widget);
    if (type == CT_MenuBarItem) {
        if (const auto *mbi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
            mbi && !mbi->icon.isNull() && !mbi->text.isEmpty()) {
            // The base sizes the item for the icon only (it draws icon-or-text), so it's
            // too narrow for the label. Reserve icon + gap + measured text width + padding.
            const int ic = pixelMetric(PM_SmallIconSize, opt, widget);
            const int tw = mbi->fontMetrics.horizontalAdvance(mbi->text);
            s.setWidth(kPad + ic + kGap + tw + kPad);
            s.setHeight(qMax(s.height(), ic));
        }
    }
    return s;
}

void MenuBarIconTextStyle::drawControl(ControlElement element, const QStyleOption *opt,
                                       QPainter *p, const QWidget *widget) const
{
    if (element == CE_MenuBarItem) {
        if (const auto *mbi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt);
            mbi && !mbi->icon.isNull() && !mbi->text.isEmpty()) {
            // 1) Background / selection only: blank icon+text so the base style paints
            //    just the item background (its icon-or-text logic then draws nothing).
            QStyleOptionMenuItem bg = *mbi;
            bg.text.clear();
            bg.icon = QIcon();
            QProxyStyle::drawControl(CE_MenuBarItem, &bg, p, widget);

            // 2) Icon + text, left-aligned with padding.
            const int ic = pixelMetric(PM_SmallIconSize, opt, widget);
            const QRect r = mbi->rect;
            const bool enabled = mbi->state & State_Enabled;
            const QRect iconRect(r.left() + kPad,
                                 r.top() + (r.height() - ic) / 2, ic, ic);
            mbi->icon.paint(p, iconRect, Qt::AlignCenter,
                            enabled ? QIcon::Normal : QIcon::Disabled);

            const QRect textRect = r.adjusted(kPad + ic + kGap, 0, -kPad, 0);
            const QPalette::ColorRole role =
                (mbi->state & State_Selected) ? QPalette::HighlightedText
                                              : QPalette::ButtonText;
            drawItemText(p, textRect,
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
                         mbi->palette, enabled, mbi->text, role);
            return;
        }
    }
    QProxyStyle::drawControl(element, opt, p, widget);
}
