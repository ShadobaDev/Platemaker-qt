#include "objects/placement.hpp"

#include "objects/artworkobject.hpp"   // loadArtwork(): a picture's own size
#include "objects/objectcontroller.hpp"
#include "objects/striplayout.hpp"
#include "recordpainter.hpp"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QPen>
#include <QWidget>

namespace StripEdit {

namespace {

//! Shortest drag, in either axis, that counts as drawing a bubble rather than clicking on the strip.
constexpr int k_minPlacementDrag = 24;

} // namespace

Placement::Placement(QGraphicsScene* scene, ObjectController& objects, const StripLayout& layout,
                     const QWidget* palette, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_objects(objects)
    , m_layout(layout)
    , m_palette(palette)
{
}

void Placement::begin(const QPointF& scenePos)
{
    m_placing         = true;
    m_placementOrigin = scenePos;

    QColor c = m_palette->palette().color(QPalette::Highlight);
    QPen pen(c);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    c.setAlpha(40);

    m_placementRubber = m_scene->addRect(QRectF(scenePos, QSizeF(0, 0)), pen, c);
    m_placementRubber->setZValue(1000);   // above everything while it is being drawn
}

void Placement::update(const QPointF& scenePos)
{
    if (m_placementRubber)
        m_placementRubber->setRect(QRectF(m_placementOrigin, scenePos).normalized());
}

void Placement::finish()
{
    QRectF r = m_placementRubber ? m_placementRubber->rect() : QRectF();
    if (m_placementRubber) {
        m_scene->removeItem(m_placementRubber);
        delete m_placementRubber;
        m_placementRubber = nullptr;
    }
    m_placing = false;

    if (m_layout.isEmpty() || !m_prototype)
        return;

    // Only a drag creates a bubble. Letting a bare click create one made every click on the artwork a
    // placement — including the click that just deselects the bubble you finished — so the canvas
    // quietly filled up with empty balloons. A click now means what it means everywhere else: deselect.
    if (r.width() < k_minPlacementDrag || r.height() < k_minPlacementDrag) {
        m_objects.selectOverlay(QString());
        return;
    }

    const int page = m_layout.pageAtSceneY(r.top());
    if (page < 0)
        return;

    const double targetWidth = m_layout.targetWidth();
    if (targetWidth <= 0)
        return;

    // **A picture, if that is what the tool places.** The drag says where and how wide; the file says
    // what — and which file is the tool's business, not this class's: it is armed with one or it places
    // balloons. Arming the Artwork tool with nothing chosen is what raises the file dialog (Editor).
    if (!m_placementArtwork.isEmpty()) {
        m_objects.requestArtwork(m_placementArtwork, r.left() / targetWidth,
                                 (r.top() - m_layout.page(page).top) / targetWidth,
                                 r.width() / targetWidth,
                                 loadArtwork(m_placementArtwork).size(),
                                 m_layout.anchorUidForPage(page));
        return;
    }

    // Whatever the active tool places — shape included: the panel is the tool's side of the question,
    // and this controller knows nothing about which tool is armed.
    ObjectRecord a = m_prototype();
    a.box = r.size().toSize();
    // The prototype's tail was placed against the panel's nominal box; re-aim it at the one just drawn,
    // just below the balloon, which is where a reader expects a new bubble to be speaking from.
    for (Tail& t : a.tails.items)
        t.tip = firstTailTip(a.box);

    // Creation is the library's: it mints the uid, hashes the asset and dedups identical content, so
    // the owner finishes this and feeds the result back — where it gets selected (see setOverlaySource).
    const double tw = m_layout.targetWidth();
    if (tw <= 0)
        return;

    // The drag was in strip pixels at the width the editor is laid out at, and the SVG about to be
    // written is in those same pixels — so the artwork's own width *is* this fraction of the page, and
    // the bubble comes back at scale 1.
    const QRectF  bounds = Painter::bounds(a);
    const QPointF origin = r.topLeft() + bounds.topLeft();
    m_objects.requestRecord(a, origin.x() / tw, (origin.y() - m_layout.page(page).top) / tw,
                            bounds.width() / tw,
                            m_layout.anchorUidForPage(page));
}

}  // namespace StripEdit
