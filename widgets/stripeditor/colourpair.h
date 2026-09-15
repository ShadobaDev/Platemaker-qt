#ifndef STRIPEDIT_COLOURPAIR_H
#define STRIPEDIT_COLOURPAIR_H

#include <QColor>
#include <QFrame>

class QToolButton;

namespace StripEdit {

/**
 * @brief The primary/secondary colour pair — **furniture**, not a tool.
 *
 * It sits in the tool column under the rail and stays there whichever tool is active, because the tools
 * that consume it (the eyedropper fills it; an applicator spends it) are ordinary tools holding a
 * *reference* to it rather than a colour of their own. That is what keeps them stateless.
 *
 * It is drawn the way every drawing application draws it — two **overlapping** swatches with *swap* and
 * *reset to black and white* beside them, inside a panel of its own. Two swatches side by side in the
 * rail read as two more tool tiles; the overlap and the frame say *this is one control, and it is not a
 * tool*. The swatches are real buttons rather than painted regions, so they keep the theme's hover and
 * focus states and stay reachable from the keyboard.
 *
 * It is **independent of the colours in ③**: setting a balloon's fill does not touch the pair, and
 * changing the pair does not touch any object. A pair that silently followed the selection would be a
 * second, invisible way of editing an object.
 *
 * The pair is a habit of the artist rather than a property of one comic, so it persists in `QSettings`
 * beside the splitter positions — not in the workspace.
 */
class ColourPair : public QFrame
{
    Q_OBJECT

public:
    explicit ColourPair(QWidget* parent = nullptr);

    [[nodiscard]] QColor primary() const { return m_primary; }
    [[nodiscard]] QColor secondary() const { return m_secondary; }

    //! Sets one half of the pair. Persists it and emits changed() when it is actually different.
    void set(const QColor& colour, bool secondary);

signals:
    //! Either half changed — by a dialog, a swap, a reset, or a tool that samples.
    void changed();

protected:
    //! Re-renders the swatches when the theme flips — their frames are drawn in the palette's colours.
    void changeEvent(QEvent* e) override;

private:
    void pick(bool secondary);   //!< Opens the colour dialog on that half.
    void store();                //!< Writes both halves to QSettings.
    void refresh();              //!< Swatch icons and tooltips follow the two colours.

    QToolButton* m_primaryButton   = nullptr;
    QToolButton* m_secondaryButton = nullptr;
    QToolButton* m_swapButton      = nullptr;
    QToolButton* m_resetButton     = nullptr;

    QColor m_primary   = Qt::black;
    QColor m_secondary = Qt::white;
};

}  // namespace StripEdit

#endif // STRIPEDIT_COLOURPAIR_H
