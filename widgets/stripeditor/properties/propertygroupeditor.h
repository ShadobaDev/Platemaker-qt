#ifndef STRIPEDIT_PROPERTYGROUPEDITOR_H
#define STRIPEDIT_PROPERTYGROUPEDITOR_H

#include <QIcon>
#include <QList>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

#include "textartifact.h"

namespace StripEdit {

//! Side of a colour chip, in pixels. Small enough to read as a swatch rather than a picture.
inline constexpr int k_swatchPx = 16;

inline constexpr int k_tailWidthMinPx  = 4;     //!< Narrower than this and a tail stops reading as one.
inline constexpr int k_tailWidthMaxPx  = 400;
inline constexpr int k_tailBendPercent = 100;   //!< A tail bends ±this, as a percentage of its length.

/**
 * @brief A named slice of an object's editable state.
 *
 * One editor is responsible for each, and only that editor writes it. `Tail` is a balloon's tails as a
 * collection; `TailItem` is one tail, the group of a selected tail object.
 */
enum class PropertyGroup { Placement, Size, Compositing, Shape, Skin, Style, Text, Tail, TailItem };

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
    using Subjects = QList<const TextArtifact*>;

    explicit PropertyGroupEditor(QWidget* parent = nullptr) : QWidget(parent) {}

    [[nodiscard]] virtual PropertyGroup group() const = 0;

    //! Shows these values. Never emits — populating is not editing.
    virtual void bind(const Subjects& subjects) = 0;

    //! Writes this group's properties into \p target, and nothing else.
    virtual void applyTo(TextArtifact& target) const = 0;

    //! The single-subject case, which is every caller until multi-selection lands.
    void bindOne(const TextArtifact& subject) { bind(Subjects{&subject}); }

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
inline void paintColourSwatch(QPushButton* swatch, const QColor& c)
{
    QPixmap pm(k_swatchPx, k_swatchPx);
    pm.fill(c);
    swatch->setIcon(QIcon(pm));
}

}  // namespace StripEdit

#endif // STRIPEDIT_PROPERTYGROUPEDITOR_H
