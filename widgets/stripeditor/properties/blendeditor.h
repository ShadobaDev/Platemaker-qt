#ifndef STRIPEDIT_BLENDEDITOR_H
#define STRIPEDIT_BLENDEDITOR_H

#include <optional>

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

#include <platemaker/models/strip_overlay.hpp>

class QComboBox;

namespace StripEdit {

//! The six modes and what to call them — the menu's entries and this control's, from one list, so a
//! row and a tick can never name the same mode differently.
[[nodiscard]] const QList<QPair<Platemaker::Models::BlendMode, QString>>& blendModes();

/**
 * @brief How an object is composited onto what is under it — **the one property every kind carries**.
 *
 * Not a `PropertyGroupEditor`: blend lives on `StripOverlay`, not on the authoring record, so it is not
 * one of a balloon's property groups and has no place in `PropertyGroupSet`. It belongs to the object
 * rather than to its drawing — which is exactly why it is here, in a control that a balloon, a piece of
 * lettering and imported artwork can all show. It is the only section a picture and a balloon share.
 *
 * A selection that disagrees shows **Mixed**: an empty current entry with a placeholder, the convention
 * the property editors already use for the same answer. Picking a mode then gives it to all of them.
 */
class BlendEditor : public QWidget
{
    Q_OBJECT

public:
    explicit BlendEditor(QWidget* parent = nullptr);

    //! Shows @p blend, or *Mixed* when the value has no single answer. Emits nothing.
    void setBlend(std::optional<Platemaker::Models::BlendMode> blend);

signals:
    //! A mode was picked. One history step, on the whole selection — the owner decides what that is.
    void blendPicked(Platemaker::Models::BlendMode blend);

private:
    QComboBox* m_combo      = nullptr;
    bool       m_populating = false;   //!< Suppresses the signal while binding, as every panel does.
};

}  // namespace StripEdit

#endif // STRIPEDIT_BLENDEDITOR_H
