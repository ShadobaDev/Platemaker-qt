#ifndef STRIPEDIT_OBJECTCONTROLLER_H
#define STRIPEDIT_OBJECTCONTROLLER_H

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>

#include "textartifact.h"

#include <platemaker/models/project_item.hpp>

#include <vector>

class QAction;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsView;
class QListWidget;
class QTransform;
class QWidget;

namespace StripEdit {

class BubblePanel;
class Layout;
class OverlayItem;

/**
 * @brief Everything the author *places* on the strip: the objects, the list, the selection, the drag.
 *
 * One sentence, no "and": it owns the overlay set and keeps three views of it in agreement — the scene
 * items, the composite-order list, and the tool-options panel showing whichever one is selected. The
 * editor above it owns the canvas and the tools; it tells this class when an authoring tool is active
 * and hands it the mouse while a placement drag is in flight.
 *
 * It **owns no persistence**. Every mutation is announced on one of the four signals below and only
 * becomes real when the owner writes it and feeds the new state back through setSource() — which is
 * why a new object's uid does not exist until the round trip completes (see \c m_selectNewOverlay).
 *
 * The strip's scene *is* the strip at 1:1, so an overlay's scene position is its library placement plus
 * its anchor page's top — no coordinate mapping layer, and the preview lands exactly where the render
 * will put it.
 */
class ObjectController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Wires itself to the collaborators it drives; it owns none of them.
     *
     * @param scene        Where the objects are drawn (the editor's canvas scene).
     * @param view         Needed for hit-testing and for "where is the author looking" on import.
     * @param list         The right-bottom list: composite order, mute toggles, selection.
     * @param panel        Tool options for the selected object — the Bubble/Text panel.
     * @param layout       Page geometry, owned by the editor; every placement question is asked of it.
     * @param dialogParent Parent for the file/message dialogs this raises.
     */
    ObjectController(QGraphicsScene* scene, QGraphicsView* view, QListWidget* list,
                     BubblePanel* panel, const Layout& layout, QWidget* dialogParent,
                     QObject* parent = nullptr);

    //! Adopts the owner's complete state after an edit round-trips back.
    void setSource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                   const ArtifactMap& artifacts);

    /**
     * @brief Which authoring tool is active.
     * @param active   Objects are selectable and a drag places a new one.
     * @param textOnly The Text tool: a placement is the same object with no balloon.
     */
    void setAuthoring(bool active, bool textOnly);

    void syncItems();    //!< Reconciles the scene items with the overlay set, by uid.
    void refreshList();  //!< Rebuilds the list from the overlay set (composite order).
    void reselect();     //!< Re-applies the current selection after the scene was rebuilt.

    //! While set, the list ⇄ scene selection callbacks are ignored: a programmatic rebuild is not a
    //! user action, and \c QGraphicsScene::clear() drops the selection on its way through.
    void setSyncing(bool on) { m_syncingList = on; }
    //! The scene deleted every item it owned; drop the now-dangling pointers without touching them.
    void forgetItems() { m_overlayItems.clear(); }

    // --- placement, driven by the editor's event filter ---
    [[nodiscard]] bool isPlacing() const { return m_placing; }
    //! True when an existing object sits under \p scenePos — the editor leaves that press to the item.
    [[nodiscard]] bool objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const;
    void beginPlacement(const QPointF& scenePos);
    void updatePlacement(const QPointF& scenePos);
    void finishPlacement();

signals:
    //! A bubble was drawn. Creation is the library's — it mints the uid and dedups identical artwork.
    void artifactCreated(const TextArtifact& artifact, double xFrac, double yFrac, double wFrac,
                         const QString& anchorInputUid);
    //! Any other edit, as the complete new state: one channel rather than one signal per gesture.
    void overlaysEdited(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                        const ArtifactMap& artifacts, const QString& undoText);
    //! Artwork drawn elsewhere should be copied into the workspace and registered at this placement.
    void artworkImportRequested(const QString& sourceFile, double xFrac, double yFrac, double wFrac,
                                const QString& anchorInputUid);
private:
    void onOverlayGeometryEdited(const QString& uid); //!< An item settled a move/resize/tail drag.
    /**
     * @brief How much bigger than its own artwork an overlay is drawn.
     *
     * \c 1.0 whenever the asset was authored at the width the strip is laid out at now, which is every
     * overlay until a chapter is re-profiled. After one, the record's \c wFrac still says how wide the
     * object is *relative to the page*, and this is what turns that back into a scale for the item.
     */
    [[nodiscard]] qreal itemScaleFor(const Platemaker::Models::StripOverlay& o, qreal naturalWidth) const;
    //! The library's rasterisation of \p a, cached by the SVG it emits. Empty if it cannot be produced.
    [[nodiscard]] QImage sharpRasterFor(const TextArtifact& a);
    void selectOverlay(const QString& uid);   //!< Selects one in the scene and the list, and loads the panel.
    void pushOverlays(const QString& undoText); //!< Emits overlaysEdited() with the current state.
    void applyPanelArtifact(const TextArtifact& a, bool commit); //!< Live edit from the panel → item (+persist).
    void deleteSelectedOverlay();
    void importArtwork();           //!< Asks for a file and drops it on the page currently in view.
    void duplicateSelectedOverlay();   //!< Copies the selected bubble a little down and right.
    void setOverlayEnabled(const QString& uid, bool on);  //!< The list's mute checkbox (deferred, see the ctor).
    void commitListOrder();                               //!< Adopts the list's row order as composite order.

    // --- collaborators, not owned ---
    QGraphicsScene* m_scene        = nullptr;
    QGraphicsView*  m_view         = nullptr;
    QListWidget*    m_list         = nullptr;
    BubblePanel*    m_bubblePanel  = nullptr;
    const Layout&   m_layout;
    QWidget*        m_dialogParent = nullptr;

    std::vector<Platemaker::Models::StripOverlay> m_overlays;   //!< The project's overlays, in composite order.
    ArtifactMap                                   m_artifacts;  //!< Their authoring records, keyed by overlay uid.
    QHash<QString, OverlayItem*>                  m_overlayItems; //!< Live scene items, keyed by overlay uid.
    /**
     * @brief Library rasterisations of styled bubbles, keyed by the SVG document itself.
     *
     * Keyed by the bytes rather than by the overlay's stored hash, and rendered from those same bytes
     * rather than from the file — because the file is the wrong thing to ask. A workspace can live on a
     * synced drive, where reading a file back immediately after writing it may still return the previous
     * content; keying and rendering off the buffer in hand makes the preview show what is being edited,
     * and leaves the file to matter only when a render reads it.
     *
     * Only styled bubbles are in here — an unstyled one is the same geometry either way, so rasterising
     * it would buy nothing.
     * ponytail: rasterised synchronously, on the UI thread. One bubble per settled edit is a few ms;
     * opening a chapter with dozens of styled bubbles is the case that would want QtConcurrent.
     */
    QHash<QString, QImage>                        m_sharpCache;
    QString            m_selectedOverlay;                   //!< uid of the selected overlay, empty for none.
    // Duplicate / Delete, shared by the artifact list's context menu and its keyboard shortcuts, and
    // reachable from the canvas too — the two places a bubble is ever selected.
    QAction*           m_actDuplicate    = nullptr;
    QAction*           m_actDelete       = nullptr;
    QAction*           m_actImport       = nullptr;   //!< Bring in artwork drawn outside Platemaker.
    QGraphicsRectItem* m_placementRubber = nullptr;         //!< Rubber band while a new bubble is drawn.
    QPointF            m_placementOrigin;                   //!< Where that drag started, in scene coordinates.
    bool               m_placing         = false;
    bool               m_syncingList     = false;           //!< Guards the list ⇄ scene selection round-trip.
    //! Set when this controller asked for a new bubble; the uid only exists after the owner mints it, so
    //! the selection has to wait for the feed to come back.
    bool               m_selectNewOverlay = false;
    bool               m_authoring       = false;  //!< A tool that authors objects is active.
    bool               m_textOnly        = false;  //!< That tool is Text: a placement gets no balloon.
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTCONTROLLER_H
