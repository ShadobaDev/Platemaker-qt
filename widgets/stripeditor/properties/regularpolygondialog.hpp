/**
 * \file regularpolygondialog.hpp
 * \brief The *Regular polygon / star…* dialog: a Polygon's nodes made from a few numbers.
 */

#ifndef STRIPEDIT_REGULARPOLYGONDIALOG_HPP
#define STRIPEDIT_REGULARPOLYGONDIALOG_HPP

#include <QDialog>
#include <QList>

#include "objectrecord.hpp"   // ShapeNode

namespace Ui {
class RegularPolygonDialog;
}

namespace StripEdit {

/**
 * \brief Points, an inner radius, a turn and an irregularity, which make a regular polygon or a star
 *        (Geometry::regularNodes()). It says each new set of nodes as it is made, so the balloon can show
 *        it; what OK and Cancel do with the balloon is the opener's.
 */
class RegularPolygonDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegularPolygonDialog(QWidget* parent = nullptr);
    ~RegularPolygonDialog() override;

    //! The nodes the numbers make now.
    [[nodiscard]] QList<ShapeNode> nodes() const;

signals:
    void nodesChanged(const QList<ShapeNode>& nodes);   //!< A number moved, or Re-roll was pressed.

private:
    Ui::RegularPolygonDialog* ui;   //!< The form (regularpolygondialog.ui).
    quint32 m_seed = 0;             //!< What the irregularity is dealt from; Re-roll deals it again.
};

}  // namespace StripEdit

#endif // STRIPEDIT_REGULARPOLYGONDIALOG_HPP
