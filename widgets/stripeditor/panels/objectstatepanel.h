#ifndef STRIPEDIT_OBJECTSTATEPANEL_H
#define STRIPEDIT_OBJECTSTATEPANEL_H

#include <QHash>
#include <QWidget>

#include "propertygroupset.h"

class CollapsibleSection;
class QLabel;
class QPushButton;
class QTimer;

namespace StripEdit {

class TailEditor;
class TailListEditor;

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

    /**
     * @brief Shows tail @p index of \p a — a tail selected on its own. Emits nothing.
     *
     * One section, the tail's, and *Delete tail*. The balloon's own groups are the balloon's, and showing
     * them here would make an edit look as if it belonged to the tail. Edits still arrive as the whole
     * artifact through changed() and committed(), with only that tail written.
     */
    void setTail(const TextArtifact& a, int index);

    /**
     * @brief Shows @p objects as one subject — the **union** of what they have in common. Emits nothing.
     *
     * The sections that appear are those at least one selected object carries; a control whose value
     * differs across the selection says **Mixed** rather than showing the first object's. An edit then
     * reaches every object that has that property and leaves the others exactly as they were, which is
     * what `PropertyGroupEditor::applyEditedTo()` is for.
     *
     * Today that is the two colour groups: a swatch is the one control that can say *Mixed*. The rest of
     * the typography is hidden while a set is bound rather than showing one object's numbers.
     */
    void setArtifacts(const QList<TextArtifact>& objects);

    /**
     * @brief Names a selection of @p count things of **different kinds** — objects and tails together.
     *
     * The union of their roles is *position*, and position is not edited here: it is edited by dragging.
     * So the panel says how many things are selected and offers nothing else, which §6.4 calls a
     * legitimate state — "these things have nothing in common but where they are". Delete still acts on
     * all of them.
     */
    void setMixedSubjects(int count);


    //! Nothing is selected: the sections go away and the panel says why.
    void clearSelection();

    //! Puts the caret in the text box — called right after a bubble is placed, so you can just type.
    void focusText();

signals:
    void changed(const TextArtifact& a);    //!< Continuous — for the live preview.
    void committed(const TextArtifact& a);  //!< Debounced / discrete — persist + undo.

    //! The same two, for a set: the objects in the order they were given to setArtifacts().
    void changedMany(const QList<TextArtifact>& objects);
    void committedMany(const QList<TextArtifact>& objects);
    void fitRequested();                    //!< "Fit to text" — the editor resizes the selected bubble.
    void deleteRequested();                 //!< Removes the selected object.

private:
    void onControlChanged();   //!< A group reported an edit → collect, preview, arm the commit timer.
    //! Shows the sections this artifact's kind has, and hides the rest.
    void applyKindVisibility();
    //! Remembers which sections are open, so the choice follows the artist rather than the object.
    void restoreExpansion();

    PropertyGroupSet m_groups;
    TailListEditor*  m_tailList = nullptr;   //!< A balloon's tails, as a collection.
    TailEditor*      m_tail     = nullptr;   //!< One selected tail.
    int              m_tailIndex = -1;       //!< The tail on show, or -1 when the subject is the balloon.

    QLabel*             m_subject   = nullptr;  //!< Names what is being edited.
    QLabel*             m_emptyHint = nullptr;  //!< Stands in for the sections when nothing is selected.
    QWidget*            m_actions   = nullptr;  //!< Fit / Delete — they act on the selection.
    QString             m_emptyText;            //!< What the hint says with nothing selected.
    QPushButton*        m_fitButton    = nullptr;
    QPushButton*        m_deleteButton = nullptr;   //!< "Delete", or "Delete tail" when a tail is the subject.
    QTimer*             m_commitTimer = nullptr;
    QHash<int, CollapsibleSection*> m_sections; //!< Keyed by PropertyGroup.

    TextArtifact m_artifact;
    QList<TextArtifact> m_subjects;   //!< The whole selection, when there is more than one of it.        //!< Working copy of the selected object.
    bool m_populating   = false;    //!< Suppresses change signals while binding.
    //! One artifact is bound, so an edit may be emitted about it. False for a set — there is no single
    //! object those controls would be describing.
    bool m_hasArtifact = false;
    //! How many objects the panel is speaking for. Delete acts on all of them; 0 means nothing is selected.
    int  m_selectionCount = 0;
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTSTATEPANEL_H
