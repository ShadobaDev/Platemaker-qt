#ifndef MENUBARICONTEXTSTYLE_HPP
#define MENUBARICONTEXTSTYLE_HPP

#include <QProxyStyle>

/**
 * @brief Draws icon + text together on QMenuBar top-level items.
 *
 * QMenuBar (via QCommonStyle) paints a top-level item's icon OR its text, never both —
 * so setting an icon on a menu action hides its label. This proxy restores the
 * icon + text pair and reserves the right width for it (from the text's font metrics,
 * since the base only measures the icon). Every other control is delegated unchanged to
 * the underlying platform style.
 */
class MenuBarIconTextStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    /**
     * @brief Widens menu-bar items so both the icon and the label fit.
     *
     * For a @c CT_MenuBarItem that has both an icon and text, the base style only
     * measures the icon (it draws icon-or-text), leaving the label truncated. This
     * override reserves @c kPad + icon + @c kGap + text-width + @c kPad, measuring the
     * text from the option's font metrics. All other content types fall through to the
     * base style unchanged.
     *
     * @param type   Contents type being measured.
     * @param opt    Style option (cast to QStyleOptionMenuItem for menu-bar items).
     * @param size   Base contents size.
     * @param widget Widget being laid out, if any.
     * @return The size hint, widened for icon+text menu-bar items.
     */
    QSize sizeFromContents(ContentsType type, const QStyleOption *opt,
                           const QSize &size, const QWidget *widget) const override;

    /**
     * @brief Paints icon + text together for menu-bar items.
     *
     * For a @c CE_MenuBarItem that has both an icon and text, it first lets the base
     * style paint only the item background/selection (by blanking the icon and text on a
     * copy of the option), then draws the icon and the label side by side. Every other
     * control element is delegated to the base style unchanged.
     *
     * @param element Control element being drawn.
     * @param opt     Style option (cast to QStyleOptionMenuItem for menu-bar items).
     * @param p       Painter to draw with.
     * @param widget  Widget being painted, if any.
     */
    void drawControl(ControlElement element, const QStyleOption *opt,
                     QPainter *p, const QWidget *widget) const override;

private:
    static constexpr int kPad = 10;  //!< Left/right padding inside the item (also the hover margin).
    static constexpr int kGap = 4;   //!< Gap between icon and text.
};

#endif // MENUBARICONTEXTSTYLE_HPP
