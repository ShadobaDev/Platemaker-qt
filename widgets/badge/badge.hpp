#ifndef BADGE_HPP
#define BADGE_HPP

#include <QColor>
#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>

#include <functional>

class QFont;
class QPainter;
class QPalette;
class QWidget;

/**
 * @brief A small rounded chip: a few words on a coloured ground, with a tooltip behind it.
 *
 * Used in two shapes that used to have nothing in common — painted inside an item view's rows, and
 * standing on its own in the status bar. A delegate cannot hand out widgets and a status bar cannot
 * host a delegate, so what they share is this description plus the painter below.
 *
 * **One colour is required and three are derived**, which is what lets a caller override exactly the
 * one it cares about. The derivations are the conventions this application already used by hand:
 * a border of the same hue a step darker, and a label in whichever of black or white can be read on
 * the fill. That last one replaces a literal dark grey that was correct only while every chip stayed
 * light — a red error chip printed dark text on a dark ground.
 */
struct Badge
{
    QString text;         //!< Four words at most. It is a chip, not a sentence.
    QColor  fill;         //!< The only colour a caller must supply.
    QString detail{};     //!< Tooltip: what this means and what to do. Never omit it on an error.
    QColor  fill2{};      //!< Invalid → flat. Valid → a vertical gradient from @c fill down to this.
    QColor  border{};     //!< Invalid → @c fill darkened a step.
    QColor  textColour{}; //!< Invalid → black or white, whichever reads on @c fill.

    // Chainable overrides, so one colour can be changed without restating the others.
    Badge& withFill2(const QColor& c)      { fill2 = c;      return *this; }
    Badge& withBorder(const QColor& c)     { border = c;     return *this; }
    Badge& withTextColour(const QColor& c) { textColour = c; return *this; }
};

//! So a chip can be carried in a model's item data, which is how a view's rows report without the
//! delegate having to know what kind of thing each row stands for.
Q_DECLARE_METATYPE(Badge)

/**
 * @brief What a chip means. The three advisory levels plus the tone that means nothing at all.
 *
 * Neutral is for the chips that **report** rather than advise — *blend*, *muted*, *Shout* on an object
 * row. They are not a fourth severity; they are the absence of one.
 */
enum class BadgeTone { Info, Warning, Error, Neutral };

/**
 * @brief A chip in one of the four tones — the one place their colours are decided.
 *
 * Derived against @p palette rather than fixed, so the same call reads on a light theme and on a dark
 * one: the hue carries the meaning and the lightness follows the window it is drawn in. Nowhere else in
 * this application names a badge colour.
 */
[[nodiscard]] Badge toneBadge(BadgeTone tone, const QString& text, const QString& detail,
                              const QPalette& palette);

//! The size one chip wants, at @p base's scale (the chip's font is derived from it).
[[nodiscard]] QSize badgeSize(const Badge& badge, const QFont& base);

//! Paints one chip filling @p chip exactly, deriving whatever colours the caller left invalid.
void paintBadge(QPainter& painter, const QRect& chip, const Badge& badge, const QFont& base);

/**
 * @brief Lays a run of chips out left to right from @p left, and paints them when @p painter is given.
 *
 * **One code path for drawing and for hit-testing.** Pass a null painter to measure — that is how a
 * view answers "which chip is under the cursor" without a second copy of the layout arithmetic, which
 * is the copy that drifts and puts the wrong tooltip on a chip.
 *
 * Chips are vertically centred in the @p lineHeight band starting at @p top, and one that would cross
 * @p right is dropped along with everything after it. The returned list therefore has one rect per chip
 * *drawn*, which may be fewer than @p badges — index into it, never into @p badges.
 */
// Not [[nodiscard]]: when it is painting, the rectangles are a by-product nobody needs.
QList<QRect> layOutBadges(QPainter* painter, const QFont& base,
                                        const QList<Badge>& badges,
                                        int left, int top, int lineHeight, int right);

/**
 * @brief One chip as a widget, for a status bar or any layout.
 *
 * Carries its own tooltip, so unlike the painted form there is nothing for the host to answer.
 *
 * Given @p onClick it becomes clickable, with a pointing-hand cursor to say so. That is what turns a
 * chip reporting a problem into a chip offering the way out of it — a status bar has no room for a
 * label and a button, and a chip nobody can act on is a chip that only nags.
 */
[[nodiscard]] QWidget* makeBadge(const Badge& badge, QWidget* parent = nullptr,
                                 std::function<void()> onClick = {});

#endif // BADGE_HPP
