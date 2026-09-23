#ifndef STRIPEDIT_PROPERTYGROUPEDITOR_HPP
#define STRIPEDIT_PROPERTYGROUPEDITOR_HPP

#include <QCheckBox>
#include <QComboBox>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QList>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

#include "propertygroup.hpp"   // PropertyGroup, and which groups a record carries

namespace StripEdit {

/**
 * @brief How long typing is coalesced before it becomes one history step.
 *
 * Part of the panels' shared contract rather than one panel's number: ③'s two object panels both
 * debounce, and two copies of a timing that is supposed to feel the same is how they come to feel
 * different.
 */
inline constexpr int k_commitDebounceMs = 300;


//! Side of a colour chip, in pixels. Small enough to read as a swatch rather than a picture.
inline constexpr int k_swatchPx = 16;

inline constexpr int k_styleAmountMin = 0;    //!< Per cent of the style's own strength.
inline constexpr int k_styleAmountMax = 200;

inline constexpr int k_textSizeMin = 6;      //!< Below this the lettering stops being lettering.
inline constexpr int k_textSizeMax = 400;

inline constexpr int k_strokeWidthMin = 0;    //!< A balloon with no outline at all is a legitimate look.
inline constexpr int k_strokeWidthMax = 40;

inline constexpr int k_tailWidthMinPx  = 4;     //!< Narrower than this and a tail stops reading as one.
inline constexpr int k_tailWidthMaxPx  = 400;
inline constexpr int k_tailBendPercent = 100;   //!< A tail bends ±this, as a percentage of its length.

/**
 * @brief One property group's controls, bound to whatever is selected.
 *
 * The contract that stops two editors overwriting each other. An editor **reads** its subject through
 * bind() and **writes** through applyTo(), which touches only the properties its group owns — so
 * "an editor wrote a field that was not its own" becomes unstateable rather than something to
 * remember. The panel that used to do this kept a whole copy of the selected artifact and wrote the
 * whole copy back on every control change, which is why a canvas resize had to be pushed into it by
 * hand to stop the stale copy writing the old box back.
 *
 * An editor **never knows which panel it is in**. If it has to be more compact under the tool rail
 * than on the right, the surface asks; the editor does not ask where it is. The previous attempt
 * carried an enum naming its own seat, and the visible result was two panels showing the same
 * controls a few hundred pixels apart.
 *
 * Two signals, following the contract both existing panels already use: \c edited() continuously,
 * for the live preview, and \c committed() once for a settled edit, which is one undo step.
 */
class PropertyGroupEditor : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief What is being edited — one artifact today, several once multi-selection lands.
     *
     * Plural from the first line of code on purpose: retrofitting many subjects into every editor
     * later is the expensive version of this change. Until a selection model can produce more than
     * one, an editor binding several may show the first; rendering a disagreement as *Mixed* arrives
     * with multi-selection, which is the increment that can also produce it.
     */
    using Subjects = QList<const Artifact*>;

    explicit PropertyGroupEditor(QWidget* parent = nullptr) : QWidget(parent) {}

    [[nodiscard]] virtual PropertyGroup group() const = 0;

    //! Shows these values. Never emits — populating is not editing.
    virtual void bind(const Subjects& subjects) = 0;

    //! Writes this group's properties into \p target, and nothing else.
    virtual void applyTo(Artifact& target) const = 0;

    //! The single-subject case, which is every caller until multi-selection lands.
    void bindOne(const Artifact& subject) { bind(Subjects{&subject}); }

    /**
     * @brief Writes into @p target only the properties the artist has **touched** since bind().
     *
     * The whole-group write is right for one object and wrong for several: it would stamp the first
     * object's other values onto everything else in the selection. Editors that can be bound to a set
     * override this; the default is the group, which is what a single subject wants.
     */
    virtual void applyEditedTo(Artifact& target) const { applyTo(target); }

signals:
    void edited();     //!< A control moved — live preview, no history step.
    void committed();  //!< The edit settled, or a dialog returned — one history step.
};

/**
 * @brief Puts \p c on \p swatch as an icon.
 *
 * An icon rather than a stylesheet: the button keeps the theme's own look (see the "inherit, don't
 * hardcode colours" rule) and only carries the chosen colour as a chip.
 */
/**
 * @brief Shows @p c on @p swatch — framed, chequered under transparency, and named in the tooltip.
 *
 * A bare square of colour is unreadable at both ends of the range: black on a dark theme and white on a
 * light one vanish into the button, which is exactly when the artist most needs to see what they picked.
 * The frame is the palette's text colour, so it works in either theme, and the chequer is what tells a
 * 10% alpha fill from a pale one. The hex goes in the tooltip because a swatch cannot be read aloud.
 */
inline void paintColourSwatch(QPushButton* swatch, const QColor& c)
{
    const QPalette& pal = swatch->palette();
    QPixmap pm(k_swatchPx, k_swatchPx);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    const QRectF box(0.5, 0.5, k_swatchPx - 1, k_swatchPx - 1);
    if (c.alpha() < 255) {
        const qreal half = k_swatchPx / 2.0;
        p.fillRect(box, pal.color(QPalette::Base));
        p.fillRect(QRectF(box.left(), box.top(), half, half), pal.color(QPalette::Mid));
        p.fillRect(QRectF(box.left() + half, box.top() + half, half, half), pal.color(QPalette::Mid));
    }
    p.fillRect(box, c);
    QColor frame = pal.color(QPalette::Text);
    frame.setAlpha(160);
    p.setPen(frame);
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);
    p.end();

    swatch->setIcon(QIcon(pm));
    swatch->setToolTip(c.name(QColor::HexArgb));
}

/**
 * @brief Shows @p value in @p spin, or **Mixed** when the selection disagrees.
 *
 * A spin box has no third state, so the value one below its minimum becomes the sentinel and Qt's own
 * `specialValueText` renders it. Stepping or typing leaves the sentinel behind, which is what tells the
 * editor the artist has taken a position — see restoreSpin().
 */
inline void showSpin(QSpinBox* spin, bool mixed, int value, int min, int max)
{
    const QSignalBlocker block(spin);
    if (mixed) {
        spin->setRange(min - 1, max);
        spin->setSpecialValueText(QObject::tr("Mixed"));
        spin->setValue(min - 1);
    } else {
        spin->setSpecialValueText(QString());
        spin->setRange(min, max);
        spin->setValue(value);
    }
}

//! Takes the Mixed sentinel away once the artist has moved @p spin. Returns false if it was never there.
inline bool restoreSpin(QSpinBox* spin, int min)
{
    if (spin->minimum() == min)
        return false;
    const QSignalBlocker block(spin);
    spin->setSpecialValueText(QString());
    spin->setRange(min, spin->maximum());
    return true;
}

/**
 * @brief Shows entry @p index in @p combo, or **Mixed** when the selection disagrees.
 *
 * No fake entry in the list: an unset combo shows its placeholder, so *Mixed* cannot be picked by
 * accident and cannot end up written to anything.
 */
inline void showCombo(QComboBox* combo, bool mixed, int index)
{
    const QSignalBlocker block(combo);
    combo->setPlaceholderText(QObject::tr("Mixed"));
    combo->setCurrentIndex(mixed ? -1 : index);
}

//! Shows @p on in @p box, or **Mixed** as Qt's own partially-checked state.
inline void showCheck(QCheckBox* box, bool mixed, bool on)
{
    const QSignalBlocker block(box);
    box->setTristate(mixed);
    box->setCheckState(mixed ? Qt::PartiallyChecked : (on ? Qt::Checked : Qt::Unchecked));
}

/**
 * @brief Marks a colour swatch as **Mixed** — the selection disagrees about this colour.
 *
 * A chequer rather than one of the colours, because showing any single value here would be the panel
 * claiming something about objects that do not have it. Pressing it still picks, and the pick then
 * lands on every selected object that has this colour.
 */
inline void paintMixedSwatch(QPushButton* swatch, const QPalette& pal)
{
    QPixmap pm(k_swatchPx, k_swatchPx);
    pm.fill(pal.color(QPalette::Base));
    QPainter p(&pm);
    const int half = k_swatchPx / 2;
    p.fillRect(0, 0, half, half, pal.color(QPalette::Mid));
    p.fillRect(half, half, k_swatchPx - half, k_swatchPx - half, pal.color(QPalette::Mid));
    p.end();
    swatch->setIcon(QIcon(pm));
}

}  // namespace StripEdit

#endif // STRIPEDIT_PROPERTYGROUPEDITOR_HPP
