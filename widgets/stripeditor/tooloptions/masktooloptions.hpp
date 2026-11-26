#ifndef STRIPEDIT_MASKTOOLOPTIONS_HPP
#define STRIPEDIT_MASKTOOLOPTIONS_HPP

#include "objects/maskarea.hpp"
#include "toolrail/toolregistry.hpp"

#include <QWidget>

class QFormLayout;
class QLabel;
class QSpinBox;
class QToolButton;

namespace StripEdit {

/**
 * \brief Options for the mask tools, one page for the three: whether what they draw is added to the selected mask
 *        (it hides more) or subtracted, and each tool's own: the brush's size, the shape's Rectangle or Ellipse, the
 *        lasso's Freehand or Polygonal.
 *
 * Remembered in `QSettings`, a working preference that follows the artist, as the other tools' options are.
 */
class MaskToolOptions : public QWidget
{
    Q_OBJECT

public:
    explicit MaskToolOptions(QWidget* parent = nullptr);

    void setTool(MaskTool tool);   //!< Shows @p tool's rows; stroke() is its from now on.

    [[nodiscard]] bool       adds() const;     //!< Add; false for Subtract.
    [[nodiscard]] int        size() const;     //!< The brush's diameter, in strip pixels.
    [[nodiscard]] MaskStroke stroke() const;   //!< How the tool shown draws.
    //! Says that a mask must be selected, unless @p paints (ObjectController::canPaintMask()).
    void showTarget(bool paints);

signals:
    void changed();   //!< adds(), size() or stroke() changed.

private:
    QFormLayout* m_form      = nullptr;
    QToolButton* m_add       = nullptr;
    QToolButton* m_subtract  = nullptr;
    QSpinBox*    m_size      = nullptr;   //!< The brush's.
    QWidget*     m_shape     = nullptr;   //!< The shape's row: Rectangle or Ellipse.
    QToolButton* m_ellipse   = nullptr;
    QWidget*     m_lasso     = nullptr;   //!< The lasso's row: Freehand or Polygonal.
    QToolButton* m_polygonal = nullptr;
    QLabel*      m_why       = nullptr;   //!< That a mask must be selected (showTarget()).
    MaskTool     m_tool      = MaskTool::Brush;
};

}  // namespace StripEdit

#endif // STRIPEDIT_MASKTOOLOPTIONS_HPP
