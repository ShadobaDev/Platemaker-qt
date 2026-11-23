#ifndef STRIPEDIT_MASKBRUSHTOOLOPTIONS_HPP
#define STRIPEDIT_MASKBRUSHTOOLOPTIONS_HPP

#include <QWidget>

class QRadioButton;
class QSpinBox;

namespace StripEdit {

/**
 * \brief Options for the Mask brush: whether a stroke hides (the brush) or shows (the eraser), and the brush's
 *        size on the strip.
 *
 * Remembered in `QSettings`, a working preference that follows the artist, as the other tools' options are.
 */
class MaskBrushToolOptions : public QWidget
{
    Q_OBJECT

public:
    explicit MaskBrushToolOptions(QWidget* parent = nullptr);

    [[nodiscard]] bool hides() const;   //!< The brush; false for the eraser.
    [[nodiscard]] int  size() const;    //!< The brush's diameter, in strip pixels.

signals:
    void changed();   //!< hides() or size() changed.

private:
    QRadioButton* m_brush  = nullptr;
    QRadioButton* m_eraser = nullptr;
    QSpinBox*     m_size   = nullptr;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKBRUSHTOOLOPTIONS_HPP
