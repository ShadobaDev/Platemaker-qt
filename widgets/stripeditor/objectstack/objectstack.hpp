#ifndef STRIPEDIT_OBJECTSTACK_HPP
#define STRIPEDIT_OBJECTSTACK_HPP

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace StripEdit {

class ObjectController;
class PresetStore;
class StripLayout;

/**
 * @brief OBJECT STACK: every object in composite order, with the strip and its pages pinned under them.
 *
 * A **stack**: row 0 is the front-most object, and a row covers every row below it wherever they
 * overlap. A bubble's tails are rows of their own, nested under it; the strip's pages nest under the
 * strip. Each row wears its object drawn small, names it, and carries chips for what is true of it and
 * written nowhere else on the row (unanchored, the preset it still matches, a blend mode).
 *
 * The tree is a view of the ObjectController and nothing more. It reads what the controller holds, and
 * hands back what the artist did to it: a selection, a mute, a new order. The controller knows nothing
 * of this class — it announces that the rows, the selection or the subject changed, and this follows.
 *
 * Its right-click menu is the object menu, which the ObjectController still attaches.
 */
class ObjectStack : public QObject
{
    Q_OBJECT

public:
    /**
     * @param tree     The form's object stack.
     * @param objects  What the rows show, and what they report to.
     * @param layout   The strip's pages, for the strip's own rows.
     * @param presets  For the chip naming the preset an object still matches.
     * @param palette  Whose palette greys an unanchored row — the editor's.
     */
    ObjectStack(QTreeWidget* tree, ObjectController& objects, const StripLayout& layout,
                PresetStore& presets, const QWidget* palette, QObject* parent);

    //! The pages the grade skips, so their rows can say so. Touches the rows only when the set changed.
    void setExcludedPages(const QSet<QString>& inputUids);

    /**
     * @brief Brings every row in line with the controller — **updated in place, never cleared and refilled**.
     *
     * The tree is where an object is picked out precisely, and every edit comes back as a feed, so a tree
     * rebuilt on each feed would lose what the artist had opened, selected or scrolled to at exactly the
     * moment they were using it. Rows are matched by id: an overlay's uid, the strip's fixed id, a page's
     * input uid.
     */
    void refresh();

private:
    void showSelectedRows();    //!< Highlights the selected objects and tails (not a tail's carrier).
    void showSubjectRow();      //!< Highlights the selected strip or page.
    void revealSelectedRow();   //!< Scrolls the first highlighted row into view, if it is not.
    void commitOrder();         //!< Hands the rows' order, read bottom-up, to the controller.

    //! The tree row of the selected strip or page, or nullptr.
    [[nodiscard]] QTreeWidgetItem* subjectRow() const;
    //! The glyph a row wears — the object drawn small, cached until the look it is made of changes.
    [[nodiscard]] QIcon rowGlyph(const QString& uid);

    QTreeWidget*        m_tree    = nullptr;
    ObjectController&   m_objects;
    const StripLayout&  m_layout;
    PresetStore&        m_presets;
    const QWidget*      m_palette = nullptr;

    QSet<QString>       m_excludedPages;   //!< Pages the grade skips — said on their rows.

    QHash<QString, QPair<QString, QIcon>> m_glyphs;   //!< uid -> (what the glyph is made of, the glyph).
    QIcon                                 m_tailGlyph;  //!< One drawing; every tail row wears it.
    QIcon                                 m_pageGlyph;  //!< Likewise for a page…
    QIcon                                 m_stripGlyph; //!< …and for the strip itself.

    bool    m_syncing = false;   //!< Set while this class moves the rows, so the tree's signals are its own echo.
    /**
     * @brief Coalesces a drag in the tree into one commit. A tree moves a row by taking it out and inserting it
     *        again, so one gesture can arrive as more than one model signal; they all restart this, and it
     *        fires once, after the drop has finished.
     */
    QTimer* m_orderCommit = nullptr;
    /**
     * @brief Set from the moment a drag starts taking a row out until the commit above has run. Taking a row
     *        out drops its selection, and without this the tree would report a deselection nobody asked for.
     */
    bool    m_rowsMoving = false;

    //! Which kind of thing a tree row stands for, beside its id in Qt::UserRole.
    static constexpr int k_kindRole = Qt::UserRole + 1;
    //! A tail row's position in its bubble's list, beside the bubble's uid in Qt::UserRole.
    static constexpr int k_tailRole = Qt::UserRole + 2;
    //! The strip row's id. Overlay uids are minted as "ovl-…" and page ids are input uids, so it is free.
    static inline const QString k_stripId = QStringLiteral("strip");
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTSTACK_HPP
