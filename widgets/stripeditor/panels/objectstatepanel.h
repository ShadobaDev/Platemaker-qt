#ifndef STRIPEDIT_OBJECTSTATEPANEL_H
#define STRIPEDIT_OBJECTSTATEPANEL_H

#include <QHash>
#include <QWidget>

#include "propertygroupset.h"

class CollapsibleSection;
class QLabel;
class QTimer;

namespace StripEdit {

/**
 * @brief What the selected object **is** — the right-hand panel, and nothing else's business.
 *
 * It follows the selection and never the active tool. A tool chosen minutes ago is a cause the artist
 * cannot see, so it decides what a *placement* creates and nothing about what is already there.
 *
 * **Which sections exist comes from the selected object's kind.** A shapeless text object has no fill to
 * edit and no tail to grow, so those sections are *absent* rather than greyed: a greyed control promises
 * something deferred, and these are not deferred — they are not part of that object. Two things keep
 * that from making the panel restless. Section order is fixed, so a group present on both the old and
 * the new selection stays where it was; and the editors are updated in place rather than rebuilt, so
 * expansion and keyboard focus survive. The panel therefore does not move when one balloon is selected
 * after another — almost every selection — and moves exactly when the kind changes, which is worth
 * seeing.
 *
 * Shape is the exception that stays on a shapeless object: it is how a caption grows a balloon. That
 * stops being a property edit the day a conversion tool exists, and the section changes meaning with it.
 *
 * Follows the same contract as `GradePanel`: \c setArtifact() populates without emitting; editing emits
 * \c changed() continuously (live preview) and \c committed() once the controls settle (debounced) or on
 * a discrete action (persisted, one undo step).
 */
class ObjectStatePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectStatePanel(QWidget* parent = nullptr);

    //! Shows \p a and names it in the header. Emits nothing.
    void setArtifact(const TextArtifact& a);

    //! Nothing is selected: the sections go away and the panel says why.
    void clearSelection();

    //! Puts the caret in the text box — called right after a bubble is placed, so you can just type.
    void focusText();

signals:
    void changed(const TextArtifact& a);    //!< Continuous — for the live preview.
    void committed(const TextArtifact& a);  //!< Debounced / discrete — persist + undo.
    void fitRequested();                    //!< "Fit to text" — the editor resizes the selected bubble.
    void deleteRequested();                 //!< Removes the selected object.

private:
    void onControlChanged();   //!< A group reported an edit → collect, preview, arm the commit timer.
    //! Shows the sections this artifact's kind has, and hides the rest.
    void applyKindVisibility();
    //! Remembers which sections are open, so the choice follows the artist rather than the object.
    void restoreExpansion();

    PropertyGroupSet m_groups;

    QLabel*             m_subject   = nullptr;  //!< Names what is being edited.
    QLabel*             m_emptyHint = nullptr;  //!< Stands in for the sections when nothing is selected.
    QWidget*            m_actions   = nullptr;  //!< Fit / Delete — they act on the selection.
    QTimer*             m_commitTimer = nullptr;
    QHash<int, CollapsibleSection*> m_sections; //!< Keyed by PropertyGroup.

    TextArtifact m_artifact;        //!< Working copy of the selected object.
    bool m_populating   = false;    //!< Suppresses change signals while binding.
    bool m_hasSelection = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTSTATEPANEL_H
