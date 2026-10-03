#ifndef STRIPEDIT_OBJECTMENU_HPP
#define STRIPEDIT_OBJECTMENU_HPP

#include <QObject>

class QAction;
class QGraphicsView;
class QMenu;
class QWidget;

namespace StripEdit {

class ColourPair;
class ObjectController;
class PresetStore;
class StripLayout;

/**
 * @brief The object menu — one right-click menu, on the OBJECT STACK and on the canvas alike.
 *
 * The menu is the two widgets' own action list (Qt::ActionsContextMenu), so they cannot drift apart and
 * every entry keeps its shortcut. Sections are separators in that same list: what the object *looks
 * like*, then where it sits in the stack, then what happens to it as a whole.
 *
 * This holds the entries, decides which apply to the selection, and asks what only a person can answer
 * (a preset's name, a file to import). **What an entry does is the ObjectController's**: delete, blend,
 * convert, re-anchor and the rest are operations on the objects, which OBJECT STATE and the canvas call
 * too, so they live with the objects and the menu only names them.
 *
 * **A menu entry that cannot apply is not greyed, it is absent**; one that cannot apply *now* is greyed.
 * See updateEntries().
 */
class ObjectMenu : public QObject
{
    Q_OBJECT

public:
    /**
     * @param objects       What the entries act on, and whose selection decides which apply.
     * @param layout        The strip's pages, for *Re-anchor to ▸* and *Fit to strip width*.
     * @param presets       For *Apply preset ▸* and *Save as preset…*.
     * @param tree          The OBJECT STACK's tree, which gets the menu.
     * @param view          The canvas, which gets it too.
     * @param dialogParent  Parent of the submenus and the dialogs.
     */
    ObjectMenu(ObjectController& objects, const StripLayout& layout, PresetStore& presets,
               QWidget* tree, QGraphicsView* view, QWidget* dialogParent, QObject* parent);

    /**
     * @brief Where the menu's colour entries read from — the tool column's pair. Never written to.
     *
     * The pair is furniture, not a tool: the tools that spend it hold a reference rather than a colour of
     * their own, and so does this menu. Without one, the two colour entries stay hidden.
     * 
     * @param pair  The source of the menu's colour entries.
     */
    void setColourSource(const ColourPair* pair);

private:
    /**
     * @brief Which entries the selection can take: enabled, greyed or absent.
     *
     * An entry that acts on one object stays disabled while several things are selected rather than
     * quietly acting on the primary, and every selected object has to be able to take an entry, not
     * merely one of them — a menu that acts on part of what is selected is a menu that lied about its
     * subject.
     */
    void updateEntries();
    /**
     * @brief Rebuilds *Apply preset ▸* from the store, so a preset saved a moment ago is already there.
     */
    void rebuildPresetMenu();
    /**
     * @brief Rebuilds *Re-anchor to ▸* from the layout: one entry per page, the current one checked.
     */
    void rebuildReanchorMenu();
    /**
     * @brief Saves the selected object's look as a named preset — everything a preset carries, and no lettering.
     */
    void saveSelectionAsPreset();
    //! Asks for a picture and hands it to the objects to place; says why not when there is no page yet.
    void importArtwork();

    ObjectController&  m_objects;
    const StripLayout& m_layout;
    PresetStore&       m_presets;
    QWidget*           m_tree         = nullptr;
    QGraphicsView*     m_view         = nullptr;
    QWidget*           m_dialogParent = nullptr;

    /**
     * @brief *Apply preset ▸* on the selection. Restyling something that exists is a different act from
     *        choosing what the next object will be, so it lives with the object rather than with the tool.
     */
    QMenu*            m_presetMenu = nullptr;
    /**
     * @brief *Re-anchor to ▸* on the selection — the explicit way back for an object whose page is gone, and
     *         the only way to move one that is not on the strip, where there is nothing to drag.
     */
    QMenu*            m_reanchorMenu = nullptr;
    // Duplicate / Delete, shared by the object stack's context menu and its keyboard shortcuts, and
    // reachable from the canvas too — the two places a bubble is ever selected.
    QAction*           m_actDuplicate    = nullptr;
    QAction*           m_actDelete       = nullptr;
    QAction*           m_actForward      = nullptr;   //!< Bring forward — one place up the stack.
    QAction*           m_actBackward     = nullptr;   //!< Send back.
    QMenu*             m_blendMenu       = nullptr;   //!< The six blend modes, checkable, on the selection.
    QMenu*             m_convertMenu     = nullptr;   //!< The two kinds the selection can be made into.
    QAction*           m_actToText       = nullptr;   //!< Convert to ▸ Text: no silhouette at all.
    QAction*           m_actToBalloon    = nullptr;   //!< Convert to ▸ Balloon: the tiles' silhouette.
    QAction*           m_actNaturalSize  = nullptr;   //!< Artwork at 100% — the size it was drawn at.
    QAction*           m_actFitToStrip   = nullptr;   //!< Artwork as wide as the strip, and no wider.
    QMenu*             m_groupMenu       = nullptr;   //!< *Apply this group ▸*, from the tool's options.
    QAction*           m_actFill         = nullptr;   //!< Fill with the primary colour.
    QAction*           m_actOutline      = nullptr;   //!< Outline with the secondary colour.
    QAction*           m_actSavePreset   = nullptr;   //!< Saves the selected bubble's look as a named preset.
    const ColourPair*  m_colours         = nullptr;   //!< The pair the two colour entries spend.
    QAction*           m_actImport       = nullptr;   //!< Bring in artwork drawn outside Platemaker.
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTMENU_HPP
