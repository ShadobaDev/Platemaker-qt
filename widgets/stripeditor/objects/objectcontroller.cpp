#include "objects/objectcontroller.hpp"
#include "presetstore.hpp"
#include "objects/striplayout.hpp"
#include "objects/pictureobject.hpp"
#include "objects/bubbleobject.hpp"
#include "objects/object.hpp"
#include "objects/selectionchrome.hpp"
#include "chain.hpp"
#include "geometry.hpp"
#include "styles.hpp"
// blendModes(): the menu and OBJECT STATE's row name the modes from one list.
#include "properties/blendeditor.hpp"
#include "svg.hpp"

#include <QFileInfo>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <QGuiApplication>
#include <QCryptographicHash>
#include <QGraphicsScene>
#include <QGuiApplication>

#include <optional>
#include <QGraphicsView>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QSvgRenderer>
#include <QTimer>
#include <QTransform>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace StripEdit {

namespace {

//! Scene Z: the strip is 0, seam guides 1, overlays start here (in composite order).
constexpr int k_overlayZBase = 2;
//! How far a duplicate lands from its original, so it is visibly a second bubble and not a mis-click.
constexpr int k_duplicateOffset = 28;
//! Rasterised filtered bubbles held before the cache is dropped wholesale. Generous for a chapter,
//! nothing next to the page caches above.
constexpr int k_sharpCacheEntries = 64;

/**
 * \brief The words a press at @p local picks: those whose letters are there, else those whose text lies round it
 *        (the rectangle of their letters: a press between strokes is still on the text), else empty words whose
 *        frame holds it (they have no letters to press on); 0 for none.
 *
 * The frame of lettered words is not taken: an automatic frame fills the whole seat, and a press beside the text
 * picks the object.
 */
int wordsPressedAt(const Composition& c, const QPointF& local)
{
    if (const int words = Geometry::wordsAt(c, local, 0.0))
        return words;
    // The last words are drawn last, so they are tried first (as Geometry::wordsAt() does).
    for (auto w = c.words.crbegin(); w != c.words.crend(); ++w) {
        const QPainterPath text = Geometry::textOutline(c, *w);
        if (text.isEmpty() ? seated(c, *w) && Geometry::textArea(c, *w).contains(local)
                           : text.boundingRect().contains(local))
            return w->id;
    }
    return 0;
}

} // namespace

ObjectController::ObjectController(QGraphicsScene* scene, QGraphicsView* view,
                                   PresetStore& presets, const StripLayout& layout,
                                   QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_view(view)
    , m_presets(presets)
    , m_layout(layout)
{
    m_panel = new PanelBinder(m_selection,
                              {[this](const QString& uid) -> std::optional<Composition> {
                                   return m_overlayItems.contains(uid) ? std::optional<Composition>(compositionOf(uid))
                                                                       : std::nullopt;
                               },
                               [this](const ElementRef& tail) { return tailIndex(tail); },
                               [this] { return selectionBlend(); },
                               [this] {
                                   return selectionIsPicture() ? std::optional<double>(selectedPicturePercent())
                                                               : std::nullopt;
                               },
                               [this](const QStringList& uids, const QList<Composition>& all, bool commit) {
                                   applyCompositions(uids, all, commit);
                               }},
                              this);
    m_shapeEdit = new ShapeEditMode(m_selection, m_scene,
                                    {[this](const QString& uid) { return m_overlayItems.value(uid); },
                                     [this] { return static_cast<const SelectionChrome*>(m_chrome); },
                                     [this](const QString& uid) { selectOverlay(uid); },
                                     [this](const QString& uid, const Composition& c, const QString& step) {
                                         applyComposition(uid, c, /*commit=*/true, step);
                                         m_panel->show(c);
                                     }},
                                    this);

    // A font added or removed (a workspace's fonts/, Add font…) changes what a bubble's words are set in
    // without changing its composition — so nothing else here would notice: setComposition() ignores the
    // same one, and the cached outline is the stand-in's until the object is rebuilt.
    connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
        for (Object* item : std::as_const(m_overlayItems))
            if (auto* bubble = qobject_cast<BubbleObject*>(item))
                bubble->refreshFonts();
        syncItems();   // the styled ones: a fresh rasterisation, from a document set in the new font
    });

    // The scene is the other half of the selection: clicking a bubble on the strip drives the list
    // and the panel, and vice versa.
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_syncingList) return;
        // The scene has always multi-selected — items are selectable, so Ctrl+click adds to it. This used
        // to take the first object and drop the rest, which is why the canvas could only ever hold one.
        QStringList picked;
        for (QGraphicsItem* gi : m_scene->selectedItems())
            if (auto* obj = dynamic_cast<Object*>(gi))
                picked.append(obj->uid());
        selectOverlays(picked);
    });
    // An object removed while selected leaves the selection without a word to the chrome from the
    // object itself — the scene's own signal covers that case.
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &ObjectController::refreshChrome);
}

// --- pointer and selection --------------------------------------------------

PointerTarget ObjectController::pointerTargetAt(const QPointF&     scenePos,
                                                const QTransform& deviceTransform) const
{
    // The selection's grips first: they are drawn above every object, so they are what is under the
    // pointer wherever they are — even over another object.
    if (m_chrome) {
        switch (m_chrome->hitAt(scenePos).grip) {
        case Grip::Shape:       return PointerTarget::TailHandle;
        case Grip::TopLeft:
        case Grip::BottomRight: return PointerTarget::ResizeFDiag;
        case Grip::TopRight:
        case Grip::BottomLeft:  return PointerTarget::ResizeBDiag;
        case Grip::Top:
        case Grip::Bottom:      return PointerTarget::ResizeVer;
        case Grip::Left:
        case Grip::Right:       return PointerTarget::ResizeHor;
        case Grip::Handle:      return PointerTarget::TailHandle;
        case Grip::Body:
        case Grip::None:        break;
        }
    }

    auto* obj = dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform));
    if (!obj)
        return PointerTarget::BareStrip;
    if (obj->isOrphaned())
        return PointerTarget::Orphan;
    return obj->gripAtScene(scenePos) == Grip::Handle ? PointerTarget::TailHandle : PointerTarget::Object;
}

bool ObjectController::objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const
{
    if (m_chrome && m_chrome->hitAt(scenePos).object)
        return true;   // a grip of the selection, which a Create tool's press must resize, not draw over
    return dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform)) != nullptr;
}

void ObjectController::refreshChrome()
{
    if (m_chrome)
        m_chrome->refresh();
}

void ObjectController::reselect()
{
    if (m_selection.subject() == Subject::Tail) {
        selectTail(m_selection.primary(), selectedTail());   // falls back to the bubble if the tail is gone
        return;
    }
    if (m_selection.subject() == Subject::Words) {
        selectWords(m_selection.words().object, m_selection.words().element);   // or the object, when they are gone
        return;
    }
    if (m_selection.subject() == Subject::Body) {
        selectBody(m_selection.body().object, m_selection.body().element);   // or the balloon, when it is gone
        return;
    }
    if (m_selection.subject() == Subject::Strip || m_selection.subject() == Subject::Page) {
        // A page can go between feeds — an input removed — and a selection must not outlive its row.
        // The question is the layout's, not the tree's: the tree is a view of it, and is refreshed from it.
        const bool stillThere = m_selection.subject() == Subject::Strip ? !m_layout.isEmpty()
                                                             : m_layout.pageForAnchor(m_selection.page()) >= 0;
        if (stillThere)
            selectSubject(m_selection.subject(), m_selection.page());
        else
            selectOverlay(QString());
        return;
    }
    if (!m_selection.objects().isEmpty())
        selectSubjects(m_selection.picked(), m_selection.elements());   // both kinds; whatever is gone drops out
}

void ObjectController::selectStrip()
{
    selectSubject(Subject::Strip, QString());
}

void ObjectController::selectPage(const QString& inputUid)
{
    selectSubject(Subject::Page, inputUid);
}

void ObjectController::selectTail(const QString& uid, int index)
{
    const Object* item = m_overlayItems.value(uid);
    if (!item || index < 0 || index >= item->tipCount()) {
        selectOverlay(uid);   // nothing at that position: the bubble is what is left to hold
        return;
    }
    selectSubjects({}, {tailRef(uid, index)});   // the balloon comes in as the tail's carrier, not a subject
}

void ObjectController::selectWords(const QString& uid, int id)
{
    if (!hasWords({uid, id})) {
        selectOverlay(uid);   // gone: the object is what is left to hold
        return;
    }
    m_selection.selectWords({uid, id}, [this](const QString& u, int words) { return hasWords({u, words}); });
    showSelection();
}

void ObjectController::selectBody(const QString& uid, int id)
{
    if (!hasBody({uid, id})) {
        selectOverlay(uid);   // gone: the balloon is what is left to hold
        return;
    }
    m_selection.selectBody({uid, id}, [this](const QString& u, int body) { return hasBody({u, body}); });
    showSelection();
}

bool ObjectController::leaveElement()
{
    if (m_selection.subject() != Subject::Words && m_selection.subject() != Subject::Body)
        return false;
    selectOverlay(m_selection.primary());
    return true;
}

void ObjectController::onBodyPressed(const QString& uid, int id)
{
    if (m_pressPickedWords)
        return;   // the press was on the letters of words of it: those words are what it picked
    if (m_selection.body() != ElementRef{uid, id})
        selectBody(uid, id);
}

bool ObjectController::hasBody(const ElementRef& ref) const
{
    const Object* item = m_overlayItems.value(ref.object);
    if (!item || !item->composition().balloon)
        return false;
    const QList<Body>& bodies = item->composition().balloon->bodies;
    return std::any_of(bodies.cbegin(), bodies.cend(), [&ref](const Body& b) { return b.id == ref.element; });
}

bool ObjectController::hasWords(const ElementRef& ref) const
{
    const Object* item = m_overlayItems.value(ref.object);
    if (!item)
        return false;
    const QList<Words>& words = item->composition().words;
    return std::any_of(words.cbegin(), words.cend(), [&ref](const Words& w) { return w.id == ref.element; });
}

void ObjectController::reveal(Subject subject, const QString& id, int tail)
{
    if (subject == Subject::Page) {
        const int page = m_layout.pageForAnchor(id);
        if (page < 0)
            return;
        // The page's top at the view's top: its centre would put a long page's middle on screen.
        const QRectF seen = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
        m_view->centerOn(seen.center().x(), m_layout.page(page).top + seen.height() / 2.0);
        return;
    }
    const Object* item = m_overlayItems.value(id);
    if (!item)
        return;
    if (const int at = tailIndex({id, tail}); subject == Subject::Tail && at >= 0 && at < item->tipCount())
        m_view->centerOn(item->mapToScene(item->handleAt(at)));
    else if (subject == Subject::Overlay || subject == Subject::Words || subject == Subject::Body)
        m_view->centerOn(item->sceneBoundingRect().center());
}


void ObjectController::onObjectPressed(const QString& uid, int handle)
{
    m_pressPickedWords = false;
    const bool add = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;

    if (handle >= 0) {
        if (add) {
            // Ctrl gathers, here as everywhere else. The scene has already toggled the balloon's own
            // selection by the time this arrives; re-stating the whole selection puts that right.
            QList<ElementRef> tails = m_selection.elements();
            const ElementRef  t     = tailRef(uid, handle);
            if (tails.contains(t))
                tails.removeAll(t);
            else
                tails.append(t);
            QStringList objects = m_selection.objects();
            objects.removeAll(uid);   // the balloon comes back as the tail's carrier, not as a subject
            for (const QString& carrier : std::as_const(m_selection.carriers()))
                objects.removeAll(carrier);   // and so do the other selected tails' balloons
            selectSubjects(objects, tails);
        } else if (!m_selection.elements().contains(tailRef(uid, handle))) {
            selectTail(uid, handle);
        }   // a plain press on a tail already selected keeps the selection, so the drag moves all of it
        beginDrag(uid, handle);
        return;
    }
    // A press on the letters of one of several words of the one object selected picks those words: the Text
    // section and the marked frame become theirs. An object with one words is its words already.
    if (const Object* item = m_overlayItems.value(uid);
        item && !add && m_selection.objects() == QStringList{uid} && item->composition().words.size() > 1
        && item->dragGrip() != Grip::Shape) {
        const int  words = wordsPressedAt(item->composition(), item->mapFromScene(item->pressScenePos()));
        // In a chain the click goes through one level at a time: words only inside the bubble already picked
        // (or the one whose words are picked), so the first press in a bubble picks the bubble.
        const auto seatOf = [&item](int id) {
            for (const Words& w : item->composition().words)
                if (w.id == id)
                    return w.seat;
            return 0;
        };
        const int  inside = m_selection.body().element != 0 ? m_selection.body().element
                                                            : seatOf(m_selection.words().element);
        const bool chainLevel = !item->isChain() || (words != 0 && seatOf(words) == inside);
        if (words != 0 && chainLevel) {
            m_pressPickedWords = true;
            if (m_selection.words() != ElementRef{uid, words})
                selectWords(uid, words);
            beginDrag(uid, handle);
            if (uid == m_shapeEdit->edited())
                emit textFocusRequested();
            return;
        }
    }
    // A press on the balloon itself while tails of it are selected — one or several — is a press on the
    // balloon: the scene reports no change, because the balloon was selected all along, carrying them.
    const bool carrier = m_selection.carriers().contains(uid)
                      || (m_selection.subject() == Subject::Tail && uid == m_selection.primary());
    // ...unless it lands in a bubble of a chain: the bubble is picked next (bodyPressed), and going through the
    // balloon first would show its OBJECT STATE for a moment.
    const Object* pressedItem = m_overlayItems.value(uid);
    const bool    inBubble    = pressedItem && pressedItem->isChain()
                          && pressedItem->bodyAt(pressedItem->mapFromScene(pressedItem->pressScenePos())) != 0;
    if (carrier && !add && !inBubble)
        selectOverlay(uid);
    beginDrag(uid, handle);

    // In shape editing, a press on the words is a press on the text: OBJECT STATE takes the caret. (A press
    // on a node picks it in the object, which says so through pickedVertexChanged.)
    const Object* item = uid == m_shapeEdit->edited() ? m_overlayItems.value(uid) : nullptr;
    if (!item)
        return;
    if (item->dragGrip() != Grip::Shape
        && Geometry::partAt(item->composition(), item->mapFromScene(item->pressScenePos()), 0.0) == Geometry::Part::Text)
        emit textFocusRequested();
}

bool ObjectController::canReroll() const
{
    return std::any_of(m_selection.objects().cbegin(), m_selection.objects().cend(), [this](const QString& uid) {
        const Object* item = m_overlayItems.value(uid);
        return item && !m_selection.carriers().contains(uid) && usesSeed(item->composition());
    });
}

void ObjectController::rerollSelection()
{
    QStringList        uids;
    QList<Composition> compositions;
    for (const QString& uid : std::as_const(m_selection.objects())) {
        const Object* item = m_overlayItems.value(uid);
        if (!item || m_selection.carriers().contains(uid) || !usesSeed(item->composition()))
            continue;
        Composition a = item->composition();
        Edits::reroll(a, [] { return QRandomGenerator::global()->generate(); });
        uids << uid;
        compositions << a;
    }
    if (!uids.isEmpty())
        applyCompositions(uids, compositions, /*commit=*/true, tr("Re-roll"));
}

void ObjectController::beginDrag(const QString& uid, int handle)
{
    // Is this drag the selection's, or this object's alone? Grabbing a body carries the selection when
    // that body is in it; grabbing a tail carries it when *that tail* is — a balloon being selected does
    // not make aiming one of its tails a group gesture.
    const int subjects = static_cast<int>(m_selection.objects().size() + m_selection.elements().size());
    m_dragIsGroup = subjects > 1
                 && (handle >= 0 ? m_selection.elements().contains(tailRef(uid, handle))
                                 : m_selection.objects().contains(uid));

    m_dragStartPos.clear();
    m_dragStartTips.clear();
    if (!m_dragIsGroup)
        return;

    // Everything is placed from *its own* start plus the drag's delta, never nudged per mouse-move, so a
    // fast drag cannot make the formation drift apart.
    for (const QString& u : std::as_const(m_selection.objects())) {
        if (Object* item = m_overlayItems.value(u))
            m_dragStartPos.insert(u, item->pos());
    }
    for (const ElementRef& t : std::as_const(m_selection.elements())) {
        if (Object* item = m_overlayItems.value(t.object))
            m_dragStartTips.append({t, item->handleAt(tailIndex(t))});
    }
}

void ObjectController::onCompositionEdited(const QString& uid, const Composition& next)
{
    // A drag on the canvas: the document takes it first, and the object shows what the document has.
    m_document.setComposition(uid, next);
    if (Object* item = m_overlayItems.value(uid))
        item->setComposition(next);
}

void ObjectController::onObjectDragged(const QString& uid, const QPointF& delta, int handle)
{
    if (!m_dragIsGroup)
        return;   // one object moving itself, which it has already done

    for (auto it = m_dragStartPos.cbegin(); it != m_dragStartPos.cend(); ++it) {
        if (it.key() == uid)
            continue;   // the one under the mouse placed itself
        if (Object* item = m_overlayItems.value(it.key()))
            item->setPos(it.value() + delta);
    }

    for (const auto& [tail, start] : std::as_const(m_dragStartTips)) {
        if (tail.object == uid && tailIndex(tail) == handle)
            continue;   // likewise the tail being aimed
        Object* item = m_overlayItems.value(tail.object);
        if (!item)
            continue;
        // A tip lives in the balloon's own units, and the item may be drawn at a scale, so the scene
        // delta has to be divided back out before it means anything to a tail.
        const qreal k = item->scale() > 0.0 ? item->scale() : 1.0;
        item->moveHandle(tailIndex(tail), start + delta / k);
    }
}

void ObjectController::selectSubject(Subject subject, const QString& pageUid)
{
    // Everything overlay-shaped let go of first, through the one function that already knows how: the
    // scene, the tree, the actions that need an object, and the object panel.
    selectOverlay(QString());

    if (subject == Subject::Page)
        m_selection.selectPage(pageUid);
    else
        m_selection.selectStrip();
    m_panel->unbind();   // OBJECT STATE is showing the strip or a page; no object panel is answering

    emit subjectRowChanged();   // the strip's or the page's row, in the object stack

    emit subjectChanged(m_selection.subject(), m_selection.page());
}

// --- the feed: compositions in, scene items out ----------------------------------

void ObjectController::setSource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                                 const Composition::Map&                                   compositions)
{
    m_document.reset(overlays, compositions);

    // **The feed's compositions reach the objects that already exist, here and nowhere else.** This is the
    // one direction a composition travels from outside — an undo, a reopened project, an edit made through
    // another surface — and doing it before syncItems() means the geometry pass below reads each
    // object's settled composition rather than one it is about to be given. Objects built by syncItems()
    // take theirs from the seed at construction.
    for (auto it = m_overlayItems.cbegin(), end = m_overlayItems.cend(); it != end; ++it) {
        const std::optional<Composition> c = m_document.composition(it.key());
        if (c && it.value()->composition() != *c)
            it.value()->setComposition(*c);
    }
    // An undo can take the edited shape's handles away (back to an ellipse), or the object itself.
    if (const Object* edited = m_overlayItems.value(m_shapeEdit->edited()); !edited || !edited->canEditShape())
        m_shapeEdit->setOn(false);

    syncItems();
    emit stackChanged();

    // A bubble we just asked for has arrived with its minted uid. addOverlay() appends, so it is the
    // last one — select it and put the caret in the text box, because the next thing anyone wants to do
    // with a bubble they just drew is type in it.
    if (m_selectNewOverlay && !m_document.overlays().empty()) {
        m_selectNewOverlay = false;
        selectOverlay(QString::fromStdString(m_document.overlays().back().uid));
        emit textFocusRequested();
        return;
    }
    m_selectNewOverlay = false;

    // A history step brought this feed, and these are the objects it touched. Selecting one is how the
    // artist sees *what* was undone; raising its dock only says where to look. The first that survived
    // wins — a step that removed everything it touched leaves nothing to point at.
    if (!m_selectAfterFeed.isEmpty()) {
        QStringList touched;
        touched.swap(m_selectAfterFeed);   // one-shot: consumed by this feed and no other
        for (const QString& uid : touched) {
            if (!m_overlayItems.contains(uid))
                continue;
            selectOverlay(uid);
            // A long strip is exactly where an undone edit sits off-screen, so bring it into view in
            // both places the selection shows. ensureVisible() and scrollToItem() do nothing when the
            // target is already visible, which keeps an undo of what you are looking at perfectly still.
            m_view->ensureVisible(m_overlayItems.value(uid));
            emit revealSelectionRequested();
            return;
        }
        selectOverlay(QString());
        return;
    }

    // The selection may not have survived the edit (a delete, or an undo that removed it). Re-applying
    // the set drops whatever is gone and keeps the rest, because selectOverlays() filters by what exists.
    bool gone = false;
    for (const QString& uid : std::as_const(m_selection.objects()))
        gone = gone || !m_overlayItems.contains(uid);
    if (gone) {
        selectSubjects(m_selection.picked(), m_selection.elements());
        if (m_selection.objects().isEmpty())
            return;
    }

    // A tail is held by its id, so a feed that changed its balloon's other tails (an undo, a preset) leaves it
    // selected; one that took it away leaves its balloon.
    if (m_selection.subject() == Subject::Tail && selectedTail() < 0)
        selectOverlay(m_selection.primary());
    // Words, the same.
    if (m_selection.subject() == Subject::Words && !hasWords(m_selection.words()))
        selectOverlay(m_selection.primary());
    if (m_selection.subject() == Subject::Body && !hasBody(m_selection.body()))
        selectOverlay(m_selection.primary());
}

QImage ObjectController::sharpRasterFor(const Composition& a)
{
    const QByteArray svg = Svg::write(a);
    if (svg.isEmpty())
        return {};

    // Keyed by the document, so the entry cannot outlive what produced it.
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(svg, QCryptographicHash::Sha256).toHex());
    if (const auto it = m_sharpCache.constFind(key); it != m_sharpCache.constEnd())
        return it.value();

    // The render's own rasteriser, at strip scale — the scene is 1:1 with the strip, so what appears
    // here is what the committed slice will carry. From the bytes, not the path: see m_sharpCache.
    const auto raster = Platemaker::Core::StripOverlayCompositor{}.rasterizeSvgRgba(svg.toStdString(), 1.0);
    QImage img;
    if (raster.isValid()) {
        // copy(): the QImage would otherwise reference the vector's buffer, which dies with the call.
        img = QImage(raster.rgba.data(), raster.width, raster.height,
                     raster.width * 4, QImage::Format_RGBA8888).copy();
    }
    // Every settled edit mints a new hash, so a long lettering session would otherwise accumulate one
    // rasterisation per revision. Nothing here is precious — a miss costs one librsvg render.
    if (m_sharpCache.size() > k_sharpCacheEntries)
        m_sharpCache.clear();
    m_sharpCache.insert(key, img);   // cache the failure too, so a broken bubble is not retried per repaint
    return img;
}

void ObjectController::syncItems()
{
    if (!m_scene)
        return;

    // The selection chrome is the scene's like everything else, so a scene clear takes it too
    // (forgetItems()); it comes back with the first sync after.
    if (!m_chrome) {
        m_chrome = new SelectionChrome(m_view);
        m_scene->addItem(m_chrome);
    }

    // Drop items whose overlay is gone.
    QSet<QString> live;
    for (const auto& o : m_document.overlays())
        live.insert(QString::fromStdString(o.uid));
    for (auto it = m_overlayItems.begin(); it != m_overlayItems.end(); ) {
        if (live.contains(it.key())) { ++it; continue; }
        m_scene->removeItem(it.value());
        delete it.value();
        it = m_overlayItems.erase(it);
    }

    if (m_layout.isEmpty())
        return;   // no strip laid out yet — there is nothing to place an overlay against

    int z = k_overlayZBase;
    for (const auto& o : m_document.overlays()) {
        const QString uid  = QString::fromStdString(o.uid);
        const int     page = m_layout.pageForAnchor(QString::fromStdString(o.anchorInputUid));
        const bool    orphaned = page < 0 && !o.anchorInputUid.empty();

        // **The one place the two kinds are told apart**, and it is a choice of constructor rather than
        // a test repeated downstream. An overlay whose asset carries no authoring parameters — art drawn
        // elsewhere, or a file edited outside Platemaker — becomes an PictureObject: it still shows, still
        // moves and still renders, it just cannot be re-typed. That is the intended degradation, and it
        // is what makes imported artwork a first-class object rather than an error.
        // **The document's answer, not the object's**: whether the object that exists is still the right
        // kind for what the document holds.
        const bool parametric = isParametric(uid);
        Object*    item       = m_overlayItems.value(uid);

        // An overlay cannot change kind in place; if it somehow has, rebuild rather than mis-draw it.
        if (item && (item->kind() == Object::Kind::Bubble) != parametric) {
            m_scene->removeItem(item);
            delete item;
            m_overlayItems.remove(uid);
            item = nullptr;
        }
        if (!item) {
            item = parametric
                ? static_cast<Object*>(new BubbleObject(uid, *m_document.composition(uid)))
                : static_cast<Object*>(new PictureObject(uid, pictureFor(o)));
            connect(item, &Object::geometryCommitted, this, &ObjectController::onOverlayGeometryCommitted);
            connect(item, &Object::pressed,        this, &ObjectController::onObjectPressed);
            connect(item, &Object::bodyPressed,    this, &ObjectController::onBodyPressed);
            connect(item, &Object::compositionEdited, this, &ObjectController::onCompositionEdited);
            connect(item, &Object::dragging,       this, &ObjectController::onObjectDragged);
            connect(item, &Object::chromeChanged,  this, &ObjectController::refreshChrome);
            // A picture resized by its grips: OBJECT STATE's size follows it while it moves.
            if (qobject_cast<PictureObject*>(item))
                connect(item, &Object::chromeChanged, this, [this, uid] {
                    if (uid == m_selection.primary() && selectionIsPicture())
                        m_panel->showPictureScale();
                });
            // The picked node is what the menu's Delete and Point type act on, however it was picked.
            connect(item, &Object::pickedVertexChanged, m_shapeEdit, &ShapeEditMode::changed);
            // A press on a frame's handle picks its words, as a press on their letters does.
            connect(item, &Object::wordsPressed, this, &ObjectController::selectWords);
            // A double-click on the one selected balloon enters shape editing, and another leaves it.
            connect(item, &Object::doubleClicked,  this, [this](const QString& uid) {
                if (m_selection.objects() == QStringList{uid})
                    m_shapeEdit->setOn(m_shapeEdit->edited() != uid);
            });
            m_scene->addItem(item);
            m_overlayItems.insert(uid, item);
        }

        // What it shows is the document's: given at its construction, in setSource() and by every edit.
        const Composition& a = item->composition();
        if (auto* bubble = qobject_cast<BubbleObject*>(item)) {
            // A bubble whose style is an SVG filter is drawn by the library, because Qt cannot render
            // the filter. The rest keep drawing locally: same geometry, no round-trip.
            bubble->setSharpRaster(Geometry::hasFilter(a) ? sharpRasterFor(a) : QImage());
        } else if (auto* art = qobject_cast<PictureObject*>(item)) {
            art->setPicture(pictureFor(o));   // the file may have changed under it
            // A lettered picture: its words over it, as the picture describes itself (in its file's aspect, which
            // a size guessed at import may not have) — and that description is the document's from here on.
            if (const std::optional<Composition> c = m_document.composition(uid)) {
                art->setComposition(*c);
                m_document.setComposition(uid, art->composition());
            }
        }

        item->setBlend(o.blend);
        item->setOrphaned(orphaned);
        // The composition says how wide the object is relative to the page; the item draws its own artwork at
        // that artwork's own size. The ratio is the item's scale — 1.0 for everything authored at the
        // width the strip is laid out at now, which is every overlay until a chapter is re-profiled.
        const qreal k = m_layout.scaleOf(o, item->contentBounds().width());
        item->setScale(k);
        // The composition stores the artwork's top-left; the item is positioned by its balloon's. They differ
        // by the bounds offset whenever a tail reaches above or left of the balloon — and that offset is
        // in the item's own units, so it scales with it.
        item->setPos(m_layout.scenePosOf(o) - item->contentBounds().topLeft() * k);
        item->setVisible(o.enabled);
        item->setZValue(z++);
        // Selectable under **every** tool. It used to depend on an authoring tool being active, which
        // made the object panel depend on it too — and left an object that could be dragged but not
        // selected, because moving it never asked. An unanchored object is the one exception: it is
        // not on the strip, so there is nothing to select it *on*.
        item->setFlag(QGraphicsItem::ItemIsSelectable, !orphaned);
    }
}

// --- geometry back out ------------------------------------------------------

void ObjectController::writePlacement(const QString& uid, bool rehome)
{
    Object* item = m_overlayItems.value(uid);
    if (!item || m_layout.targetWidth() <= 0)
        return;   // no strip laid out; there is nothing to measure a fraction against

    // Re-anchor to whichever page the bubble now sits on. Crossing a page boundary is a normal drag,
    // and silently re-homing it is the whole point: the offset stays relative to the artwork under it.
    // Back to the artwork's own top-left, which is what the library composites at — in scene pixels,
    // so the item's own scale applies to the offset as well as to the artwork.
    // The page is the one under the box's top, the offset the drawing's top-left's.
    const qreal   k     = item->scale();
    const QPointF p     = item->pos() + item->contentBounds().topLeft() * k;
    const qreal   width = item->contentBounds().width() * k;
    const Document::Overlay* o = m_document.overlay(uid);
    if (!o)
        return;
    // Back into the durable unit, which is what keeps the stored form independent of the profile in use.
    const QString                              own  = QString::fromStdString(o->anchorInputUid);
    const std::optional<StripLayout::Anchored> home = rehome ? m_layout.anchoredAt(p, width, item->pos().y())
                                                             : std::nullopt;
    StripLayout::Anchored at = home ? *home : m_layout.anchoredOn(rehome ? QString() : own, p, width);
    at.anchor                = home ? home->anchor : own;   // with no page under it, it keeps its own
    m_document.setPlace(uid, at);
}

void ObjectController::onOverlayGeometryCommitted(const QString& uid)
{
    Object* item = m_overlayItems.value(uid);
    if (!item)
        return;

    // A drag carries the whole selection (position is the one role everything has), so the placement of
    // every object that travelled is rewritten — and the lot becomes **one** history step, because one
    // drag is one thing the artist did. A tail that travelled changes its balloon's extent, so its
    // balloon's placement is rewritten too. The *compositions* need no rewriting at all: a tail moved on the
    // object that owns it, and that object is where its composition lives.
    const bool        group = m_dragIsGroup && item->reportedDrag();
    const QStringList moved = group ? m_selection.objects() : QStringList{uid};
    for (const QString& u : moved)
        writePlacement(u);
    const int travelled = group ? selectedSubjectCount() : 1;
    m_dragIsGroup = false;

    // Only when the panel is about this one object. A set of several — or one of two kinds — is already
    // showing what it should, and rebinding it to the thing that happened to move would be the panel
    // changing subject on its own.
    if (uid == m_selection.primary() && m_selection.objects().size() == 1) {
        // A tail's drag leaves that tail selected, so the panel shows the tail again, not the balloon.
        if (m_selection.subject() == Subject::Tail)
            m_panel->showTail(item->composition(), selectedTail());
        else if (m_selection.elements().isEmpty())
            m_panel->show(item->composition());
    }
    emit stackChanged();
    pushOverlays(item->dragGrip() == Grip::Shape ? tr("Edit shape")
                 : group                         ? tr("Move %n objects", "", travelled)
                                                 : tr("Move bubble"));
}

QString ObjectController::pictureFor(const Platemaker::Models::StripOverlay& o) const
{
    const QString asset  = QString::fromStdString(o.assetPath);
    const Composition r = compositionOf(QString::fromStdString(o.uid));
    if (!r.picture)
        return asset;   // a picture placed before pictures had a composition: the overlay's file is it

    // **The picture, not the file the library renders.** Once it is lettered they differ: the overlay
    // points at a wrapper that embeds the picture and bakes the words in, and drawing that *and* the
    // words would show them twice. The composition names the picture; it sits beside the wrapper.
    return QFileInfo(asset).absolutePath() + QLatin1Char('/') + r.picture->file;
}

// --- selection, and the properties panel it binds ---------------------------

void ObjectController::selectOverlay(const QString& uid)
{
    selectOverlays(uid.isEmpty() ? QStringList{} : QStringList{uid});
}

void ObjectController::selectOverlays(const QStringList& uids)
{
    selectSubjects(uids, {});
}

int ObjectController::selectedSubjectCount() const
{
    return m_selection.subjectCount();
}

int ObjectController::selectedTail() const
{
    return tailIndex(m_selection.single());
}

ElementRef ObjectController::tailRef(const QString& uid, int index) const
{
    const Object* item = m_overlayItems.value(uid);
    const auto&   b    = item ? item->composition().balloon : std::nullopt;
    return {uid, b && index >= 0 && index < b->tails.size() ? b->tails.at(index).id : 0};
}

int ObjectController::tailIndex(const ElementRef& ref) const
{
    const Object* item = m_overlayItems.value(ref.object);
    if (!item || ref.element == 0 || !item->composition().balloon)
        return -1;
    const QList<BalloonTail>& tails = item->composition().balloon->tails;
    for (int i = 0; i < tails.size(); ++i)
        if (tails.at(i).id == ref.element)
            return i;
    return -1;
}

void ObjectController::selectSubjects(const QStringList& uids, const QList<ElementRef>& tails)
{
    // What is gone drops out: an object no longer on the strip, a tail its balloon no longer has.
    m_selection.select(uids, tails, [this](const QString& uid, int element) {
        return m_overlayItems.contains(uid) && (element == 0 || tailIndex({uid, element}) >= 0);
    });
    showSelection();
}

void ObjectController::showSelection()
{
    const QStringList&       picked      = m_selection.objects();
    const QList<ElementRef>& pickedTails = m_selection.elements();
    const QStringList&       carriers    = m_selection.carriers();

    // Shape editing is of one object, and ends when the selection becomes anything else: a row picked in
    // OBJECT STACK, or a double click on another object (a click on the strip does not reach here while
    // editing: CanvasInput keeps it).
    if (m_shapeEdit->isOn() && picked != QStringList{m_shapeEdit->edited()})
        m_shapeEdit->setOn(false);

    // One tail, on its own balloon, is a selected tail: the handle drawn hollow and OBJECT STATE showing that
    // tail. Anything else with a tail in it is a set whose only common property is where it sits.
    const bool    singleTail = m_selection.subject() == Subject::Tail;
    const Object* primary    = m_overlayItems.value(m_selection.primary());

    m_syncingList = true;
    for (auto it = m_overlayItems.begin(); it != m_overlayItems.end(); ++it) {
        it.value()->setSelected(picked.contains(it.key()));
        QList<int> focused;   // every selected tail of this balloon, each drawn hollow
        for (const ElementRef& t : pickedTails)
            if (t.object == it.key())
                focused << tailIndex(t);
        it.value()->setFocusedHandles(focused);
        it.value()->setPickedWords(m_selection.words().object == it.key() ? m_selection.words().element : 0);
        it.value()->setPickedBody(m_selection.body().object == it.key() ? m_selection.body().element : 0);
    }

    emit selectionChanged();   // the stack's rows and the menu's entries follow it
    m_syncingList = false;

    m_panel->bind();
    emit subjectChanged(m_selection.subject(), m_selection.primary());
}




// --- operations on the selection --------------------------------------------

bool ObjectController::applyColourAt(const QPointF& scenePos, const QTransform& deviceTransform,
                                     const QColor& colour)
{
    auto* bubble = dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform));
    if (!bubble || bubble->composition().picture || bubble->isOrphaned() || !colour.isValid())
        return false;   // imported artwork has no colour of its own, and an orphan is not on the strip

    // A few screen pixels of forgiveness, in the object's own units: a thin letter and a hairline
    // outline are unhittable at 100% zoom otherwise, and at 400% the same slack would swallow the fill.
    qreal onScreen = m_view ? m_view->transform().m11() : 1.0;
    onScreen *= bubble->scale();
    if (onScreen <= 0.0)
        onScreen = 1.0;
    const qreal slack = k_pickSlackPx / onScreen;

    Composition          a    = bubble->composition();
    const QPointF        local = bubble->mapFromScene(scenePos);
    const Geometry::Part part  = Geometry::partAt(a, local, slack);
    if (!Edits::paint(a, part, colour, Geometry::wordsAt(a, local, slack)))   // the words poured on
        return false;   // the transparent corner of the box is not the balloon
    const QString step = part == Geometry::Part::Text      ? tr("Apply text colour")
                       : part == Geometry::Part::Outline   ? tr("Apply outline colour")
                                                           : tr("Apply fill colour");

    // Pouring onto something that is part of a selection paints the **whole** selection, each object in
    // the role it has: a fill lands on everything with a silhouette, the lettering's colour on everything.
    // An object outside the selection is a fresh subject, and painting it selects it as any click does.
    if (m_selection.objects().size() > 1 && m_selection.objects().contains(bubble->uid())) {
        QList<Composition> next;
        next.reserve(m_selection.objects().size());
        for (const QString& uid : std::as_const(m_selection.objects())) {
            Composition each = compositionOf(uid);
            Edits::paint(each, part, colour);
            next.append(each);
        }
        applyCompositions(m_selection.objects(), next, /*commit=*/true, step);
        m_panel->show(next);
        return true;
    }

    selectOverlay(bubble->uid());
    m_panel->show(a);
    applyComposition(bubble->uid(), a, /*commit=*/true, step);
    return true;
}

void ObjectController::fitSelectionToText()
{
    Object* item = m_overlayItems.value(m_selection.primary());
    if (!item || item->composition().picture)
        return;   // a picture's box is the picture's, not its words'
    Composition a = item->composition();
    Edits::fitToText(a);   // committed even when it fits already, as before: the button was pressed
    applyComposition(m_selection.primary(), a, /*commit=*/true);
    m_panel->show(a);
}

bool ObjectController::canAddWords() const
{
    const Object* item = m_selection.objects().size() == 1 ? m_overlayItems.value(m_selection.primary()) : nullptr;
    if (!item || (m_selection.subject() != Subject::Overlay && m_selection.subject() != Subject::Words
                  && m_selection.subject() != Subject::Body))
        return false;
    const Composition& c = item->composition();
    return std::any_of(c.words.cbegin(), c.words.cend(), [&c](const Words& w) { return seated(c, w); });
}

void ObjectController::addWords()
{
    if (!canAddWords())
        return;
    const QString uid  = m_selection.primary();
    Composition   a    = m_overlayItems.value(uid)->composition();
    const int     body = m_selection.body().element;   // a bubble picked: its words, in it
    int           from = m_selection.words().element;
    for (const Words& w : std::as_const(a.words))
        if (body != 0 && from == 0 && w.seat == body)
            from = w.id;
    const int id = Edits::addWords(a, from, body);
    if (id == 0)
        return;
    applyComposition(uid, a, /*commit=*/true, tr("Add words"));
    selectWords(uid, id);
    emit textFocusRequested();   // as a new balloon's: the next thing is to type in them
}

namespace {

//! Where *Add bubble* puts the new bubble, in its neighbour's sizes: half a width to the right, below with a gap.
constexpr qreal k_newBubbleAcross = 0.5;
constexpr qreal k_newBubbleDown   = 1.25;

//! The bubble *Add bubble* adds after in @p c: @p picked, or else the last of the chain; 0 when it is no end.
int addBubbleAfter(const Composition& c, int picked)
{
    if (!c.balloon || c.balloon->bodies.isEmpty())
        return 0;
    const int from = picked != 0 ? picked : Chain::order(*c.balloon).last();
    return Chain::chainEnd(*c.balloon, from) ? from : 0;
}

} // namespace

bool ObjectController::canAddBubble() const
{
    const Object* item = m_selection.objects().size() == 1 ? m_overlayItems.value(m_selection.primary()) : nullptr;
    if (!item || (m_selection.subject() != Subject::Overlay && m_selection.subject() != Subject::Body))
        return false;
    return addBubbleAfter(item->composition(), m_selection.body().element) != 0;
}

void ObjectController::addBubble()
{
    if (!canAddBubble())
        return;
    const QString uid  = m_selection.primary();
    Composition   a    = m_overlayItems.value(uid)->composition();
    const int     from = addBubbleAfter(a, m_selection.body().element);
    QSize size;
    for (const Body& b : std::as_const(a.balloon->bodies))
        if (b.id == from)
            size = b.size;
    int added = 0;
    if (Chain::addBody(a, from, QPointF(size.width() * k_newBubbleAcross, size.height() * k_newBubbleDown), &added)
        != Chain::ChainError::None)
        return;
    applyComposition(uid, a, /*commit=*/true, tr("Add bubble"));
    selectBody(uid, added);
}

int ObjectController::presetTargetCount() const
{
    return int(std::count_if(m_selection.objects().cbegin(), m_selection.objects().cend(), [this](const QString& uid) {
        return isParametric(uid) && !m_selection.carriers().contains(uid);
    }));
}

void ObjectController::presetTargetKinds(bool& bubble, bool& text) const
{
    bubble = text = false;
    for (const QString& uid : std::as_const(m_selection.objects())) {
        if (!isParametric(uid) || m_selection.carriers().contains(uid))
            continue;
        (compositionOf(uid).balloon ? bubble : text) = true;
    }
}

void ObjectController::applyPresetToSelection(int index)
{
    if (index >= 0 && index < m_presets.presets().size())
        applyLookToSelection(m_presets.presets().at(index), tr("Apply preset"));
}

void ObjectController::resetSelectionLook()
{
    // The default is no preset: a default BubblePreset, with no uid, so the origin goes too.
    applyLookToSelection(BubblePreset{}, tr("Reset to default"));
}

void ObjectController::applyLookToSelection(const BubblePreset& preset, const QString& step)
{
    QStringList        uids;
    QList<Composition> next;
    for (const QString& uid : std::as_const(m_selection.objects())) {
        if (!isParametric(uid) || m_selection.carriers().contains(uid))
            continue;   // a picture, or a balloon only carrying a selected tail
        const Composition was = compositionOf(uid);
        // A balloon takes the preset's shape: the shape section is on screen, so the change is visible and
        // reversible. A text object keeps having none — giving it one would be a conversion.
        uids << uid;
        next << PresetStore::applied(preset, was, /*keepShape=*/!was.balloon);
    }
    if (uids.isEmpty())
        return;
    applyCompositions(uids, next, /*commit=*/true,
                 uids.size() == 1 ? step : tr("%1 (%n objects)", "", int(uids.size())).arg(step));
    m_panel->show(next);   // one as one, several as several
}

void ObjectController::setSelectionOrigin(const PresetOrigin& origin)
{
    const Object* item = m_overlayItems.value(m_selection.primary());
    if (!item || m_selection.objects().size() != 1 || item->composition().preset == origin)
        return;
    Composition a = item->composition();
    a.preset      = origin;
    applyComposition(m_selection.primary(), a, /*commit=*/true, tr("Save as preset"));
    m_panel->show(a);
}

QPointF ObjectController::settleBox(const Object* item, Composition& next) const
{
    const Composition& prev = item->composition();
    if (next.picture || sizeOf(next) == sizeOf(prev))
        return {};   // a picture is resized from its top-left, its words with it; an unchanged box moves nothing

    // **No growing on edit.** It was tried (an edit grew the box where the words overflowed, never
    // shrinking it) and taken out: deleting letters left the box grown, and any edit to a box dragged
    // smaller on purpose grew it back. The red outline says when the words do not fit; *Fit to text*
    // and the frame's arrows are how the author answers it.
    //
    // **The lettering does not move.** Whatever changed the box — *Fit*, *Convert* — the object
    // is moved so that the lettering's anchor keeps its place on the strip, and every tail tip moves
    // back by the same amount, so it keeps pointing at its speaker.
    const QPointF shift = Geometry::textAnchor(prev) - Geometry::textAnchor(next);
    if (next.balloon)
        for (BalloonTail& t : next.balloon->tails)
            t.tail.tip -= shift;
    return shift;
}

void ObjectController::applyCompositions(const QStringList& uids, const QList<Composition>& compositions,
                                    bool commit, const QString& undoText)
{
    if (uids.size() != compositions.size())
        return;   // a caller answering about objects it no longer has; writing half of it is worse

    // **Whichever kind the object is.** This loop used to reach for a BubbleObject and skip whatever
    // was not one, which, once pictures could be lettered, meant dropping their lettering without saying so.
    for (int i = 0; i < uids.size(); ++i) {
        Object* item = m_overlayItems.value(uids.at(i));
        if (!item)
            continue;
        Composition next = compositions.at(i);
        if (const auto* art = qobject_cast<const PictureObject*>(item))
            next = art->described(next);   // still this picture, in its file's aspect
        const QPointF shift = settleBox(item, next);
        // The document first, then the object shows it (itself a no-op when it has not changed).
        m_document.setComposition(uids.at(i), next);
        item->setComposition(next);
        // A textured balloon's picture is the library's: a preview rasterises the edit at once rather
        // than leaving the untextured paths up until the commit that follows (the sync's refresh).
        if (auto* bubble = qobject_cast<BubbleObject*>(item); bubble && Geometry::hasFilter(next))
            bubble->setSharpRaster(sharpRasterFor(next));
        if (!shift.isNull())
            item->setPos(item->pos() + shift * (item->scale() > 0.0 ? item->scale() : 1.0));
    }
    // A shape tile can take the edited shape's handles away (an ellipse has none yet).
    if (const Object* edited = m_overlayItems.value(m_shapeEdit->edited()); edited && !edited->canEditShape())
        m_shapeEdit->setOn(false);

    // Live edits repaint only. Persisting every keystroke would write a file and push a history step
    // per character; the panel debounces and says when it has settled.
    if (!commit)
        return;

    // **A commit is a commit, whether or not this call is what changed the value.** Skipping the step
    // when the compositions match what the objects already hold looks like it avoids an empty history entry,
    // and instead avoids every real one: a panel previews each keystroke through this same function, so
    // by the time the debounce says *settled* the objects are already holding the settled value. The
    // decision not to make a step belongs to whoever decides an edit happened, not to the write.

    // Each object's placement is measured again, because the drawing's extent moves with the composition: a
    // longer line or a heavier stroke widens it, and a line style or an outline grows it on **every**
    // side, so its top-left moves up and left while the balloon stays put. The stored placement is that
    // top-left; left as it was, the next sync would draw the balloon down and right by the growth. Not
    // re-homed — editing a property is not moving the object.
    for (const QString& uid : uids)
        writePlacement(uid, /*rehome=*/false);

    emit stackChanged();   // a row wears its object, and its label is what the object says
    pushOverlays(!undoText.isEmpty()            ? undoText
                 : uids.size() > 1              ? tr("Edit %n objects", "", uids.size())
                 : m_selection.subject() == Subject::Tail   ? tr("Edit tail")
                 : m_selection.subject() == Subject::Words  ? tr("Edit words")
                 : m_selection.subject() == Subject::Body   ? tr("Edit bubble")
                 : compositionOf(uids.first()).picture ? tr("Edit artwork")
                                                       : tr("Edit bubble"));
}

void ObjectController::applyComposition(const QString& uid, const Composition& composition, bool commit,
                                   const QString& undoText)
{
    applyCompositions(QStringList{uid}, QList<Composition>{composition}, commit, undoText);
}

void ObjectController::importPictureFile(const QString& file)
{
    if (file.isEmpty() || m_layout.isEmpty())
        return;

    // Onto whatever the author is looking at: the middle of the viewport, resolved to the page under
    // it. Dropping it at the top of the chapter would mean scrolling back to find what you just added.
    const QRectF  view   = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    const QPointF centre = view.center();

    // Record the artwork's own width as a fraction straight away rather than leaving it at 0 ("natural
    // size"). Both draw identically today; the difference shows on the next re-profile, where a logo
    // that knows its width relative to the page grows with the chapter and a "natural size" one does
    // not. Growing with the chapter is what anyone placing artwork on a page meant.
    const QPixmap                              art = loadPixmap(file);
    const std::optional<StripLayout::Anchored> at  = m_layout.anchoredAt(centre, art.width());
    if (!at)
        return;

    m_selectNewOverlay = true;
    emit pictureImportRequested(file, at->xFrac, at->yFrac, at->wFrac, art.size(), at->anchor);
}

void ObjectController::placePictureAt(const QString& file, const QPointF& scenePos)
{
    if (file.isEmpty() || m_layout.isEmpty())
        return;

    const QPixmap art = loadPixmap(file);
    if (art.isNull())
        return;

    // Centred on the cursor, because the drag carried the picture under it: dropping is the moment the
    // artist chose *there*, and the thing they were aiming was the middle of what they could see.
    // Its own pixels, one for one with the strip's.
    const QPointF                              topLeft = scenePos - QPointF(art.width(), art.height()) / 2.0;
    const std::optional<StripLayout::Anchored> at      = m_layout.anchoredAt(topLeft, art.width());
    if (!at)
        return;

    m_selectNewOverlay = true;
    emit pictureImportRequested(file, at->xFrac, at->yFrac, at->wFrac, art.size(), at->anchor);
}

bool ObjectController::selectionIsPicture() const
{
    return m_selection.objects().size() == 1 && m_selection.elements().isEmpty()
        && !isParametric(m_selection.objects().first());
}

double ObjectController::selectedPicturePercent() const
{
    const auto*  art = qobject_cast<const PictureObject*>(m_overlayItems.value(m_selection.primary()));
    if (!art || art->pixmap().width() <= 0)
        return 0.0;
    // What it is drawn at over what it was drawn as — read off the object, so a grip being dragged shows
    // its size as it goes; at rest the object is drawn at the composition's width fraction (syncItems()).
    return art->contentBounds().width() * art->scale() * 100.0 / art->pixmap().width();
}

void ObjectController::scaleSelectedPicture(double percent, bool commit)
{
    const auto* art = qobject_cast<const PictureObject*>(m_overlayItems.value(m_selection.primary()));
    if (!art || percent <= 0 || art->pixmap().width() <= 0)
        return;

    // Its composition's size, as a grip drag sets it: on the strip that many percent of the file's own width,
    // which is the box at the item's scale. The height follows the file's aspect (PictureObject::described()).
    const qreal k     = art->scale() > 0.0 ? art->scale() : 1.0;
    Composition next  = art->composition();
    const int   width = qMax(1, qRound(art->pixmap().width() * percent / 100.0 / k));
    resize(next, QSize(width, sizeOf(next).height()));
    applyComposition(m_selection.primary(), next, commit, tr("Resize artwork"));
}

void ObjectController::reanchorSelection(const QString& pageUid)
{
    if (!m_document.setAnchor(m_selection.primary(), pageUid))
        return;
    syncItems();
    emit stackChanged();
    reselect();
    pushOverlays(tr("Re-anchor"));
}

void ObjectController::deleteSelectedTail()
{
    const Object* bubble = m_overlayItems.value(m_selection.primary());
    if (!bubble)
        return;
    Composition a = bubble->composition();
    if (!Edits::removeTails(a, {selectedTail()}))
        return;

    // The balloon is what is left to hold once its tail is gone. Selected before the edit goes out, so the
    // feed that comes back finds a balloon selected, not a tail that no longer exists.
    const QString uid = m_selection.primary();
    selectOverlay(uid);
    applyComposition(m_selection.primary(), a, /*commit=*/true, tr("Delete tail"));
    m_panel->show(a);
}

void ObjectController::deleteSelectedWords()
{
    const ElementRef words = m_selection.words();
    Object*          item  = m_overlayItems.value(words.object);
    if (!item)
        return;
    Composition a = item->composition();
    if (!Edits::removeWords(a, words.element))
        return;   // the last words: a balloon keeps somewhere to type
    // The object is what is left to hold, selected before the edit goes out (as a deleted tail's).
    selectOverlay(words.object);
    applyComposition(words.object, a, /*commit=*/true, tr("Delete words"));
    m_panel->show(a);
}

void ObjectController::deleteSelectedOverlay()
{
    // In shape editing with nodes picked, Delete takes them and never the balloon.
    if (m_shapeEdit->deletePickedNodes())
        return;
    if (m_selection.subject() == Subject::Tail) {
        deleteSelectedTail();
        return;
    }
    if (m_selection.subject() == Subject::Words) {
        deleteSelectedWords();
        return;
    }
    if (m_selection.subject() == Subject::Body)
        return;   // removing a bubble splits the chain: not offered yet
    if (m_selection.objects().isEmpty())
        return;

    // A selection of two kinds is deleted as one: the objects go, and the tails of the balloons that
    // stay go with them. Highest index first, or removing tail 1 would renumber tail 2 under our feet.
    if (!m_selection.elements().isEmpty()) {
        QStringList doomedObjects = m_selection.objects();
        for (const QString& carrier : std::as_const(m_selection.carriers()))
            doomedObjects.removeAll(carrier);   // it only lent a tail; nobody asked for the balloon

        // A balloon the artist *did* pick goes whole, so its own selected tails need no separate removal.
        QHash<QString, QList<int>> byBubble;
        for (const ElementRef& t : std::as_const(m_selection.elements())) {
            if (!doomedObjects.contains(t.object))
                byBubble[t.object].append(tailIndex(t));
        }
        const int subjects = selectedSubjectCount();

        for (auto it = byBubble.begin(); it != byBubble.end(); ++it) {
            Object* bubble = m_overlayItems.value(it.key());
            if (!bubble)
                continue;
            Composition a = bubble->composition();
            Edits::removeTails(a, it.value());
            m_document.setComposition(it.key(), a);
            bubble->setComposition(a);
            writePlacement(it.key());
        }

        m_document.remove(doomedObjects);
        // Their compositions go with their overlays (Document::remove()), and syncItems() drops their items.
        selectOverlay(QString());
        syncItems();
        emit stackChanged();
        pushOverlays(tr("Delete %n objects", "", subjects));
        return;
    }

    // One history step for the gesture, not one per object: the artist deleted a selection, and that is
    // what they will expect one Ctrl+Z to bring back.
    const QStringList doomed = m_selection.objects();
    m_document.remove(doomed);
    selectOverlay(QString());
    syncItems();
    emit stackChanged();
    pushOverlays(doomed.size() == 1 ? tr("Delete bubble")
                                    : tr("Delete %n objects", "", static_cast<int>(doomed.size())));
}



void ObjectController::applyColourToSelection(const QColor& colour, Geometry::Part role)
{
    if (!colour.isValid() || m_selection.objects().isEmpty())
        return;

    QList<Composition> next;
    int                changed = 0;
    for (const QString& uid : std::as_const(m_selection.objects())) {
        Composition each = compositionOf(uid);
        if (Edits::paint(each, role, colour))
            ++changed;
        next.append(each);
    }
    if (changed == 0)
        return;   // nothing in the selection has that role, so there is nothing to undo either

    const QString step = role == Geometry::Part::Text      ? tr("Apply text colour")
                       : role == Geometry::Part::Outline   ? tr("Apply outline colour")
                                                         : tr("Apply fill colour");
    applyCompositions(m_selection.objects(), next, /*commit=*/true, step);
    m_panel->show(next);   // one as one, several as several
}

BalloonLook ObjectController::toolBalloon() const
{
    return m_toolDefaults.balloon ? m_toolDefaults.balloon() : BalloonLook();
}

ShapeProperties ObjectController::toolBalloonShape() const
{
    return toolBalloon().shape;
}

void ObjectController::convertSelectionTo(const ShapeProperties& shape)
{
    if (m_selection.objects().isEmpty())
        return;

    // **What passes through is decided by the kind, not by the silhouette.** @p shape carries which
    // silhouette to arrive at, but a Thought balloon asked to become a Balloon is already one — and
    // re-shaping it to whatever TOOL OPTIONS happens to show would be this menu quietly doing the shape
    // picker's job on an object the artist only had along for the ride. Changing *which* balloon several
    // objects are is the Shape section's, or *Apply preset ▸*.
    const bool toBalloon = shape.kind != ShapeProperties::Kind::None;
    // To a balloon, the whole shape asked for (a kind alone would arrive without the nodes and the radius
    // that make it the shape the tiles say), in the style TOOL OPTIONS shows; to text, the balloon goes, its
    // tails with it (undo brings them back).
    const BalloonLook look{shape, toolBalloon().style};

    QList<Composition> next;
    int                converted = 0;
    for (const QString& uid : std::as_const(m_selection.objects())) {
        Composition each = compositionOf(uid);
        // Not an authored object, or already of this kind: it passes through, and since nothing here
        // touches the order, it keeps its place in the stack for free.
        // Where it lands is applyCompositions()' business — the words stay put and the balloon appears under them.
        const bool authored = isParametric(uid) && !m_selection.carriers().contains(uid);
        if (authored && Edits::convert(each, toBalloon, look))
            ++converted;
        next.append(each);
    }
    if (converted == 0)
        return;   // everything was already that kind — not an edit, and not a history step

    // Named after the kind it arrived at — *Balloon*, not *Speech balloon*: the silhouette it happens
    // to wear is a property, and a history entry should say what the step decided.
    const QString what = toBalloon ? tr("a balloon") : tr("text");
    applyCompositions(m_selection.objects(), next, /*commit=*/true,
                 converted == 1 ? tr("Convert to %1").arg(what)
                                : tr("Convert %n objects to %1", "", converted).arg(what));

    m_panel->show(next);   // one as one, several as several
}

std::optional<Platemaker::Models::BlendMode> ObjectController::selectionBlend() const
{
    return m_document.blendOf(m_selection.picked());
}

void ObjectController::setSelectionBlend(Platemaker::Models::BlendMode blend)
{
    const QStringList changed = m_document.setBlend(m_selection.picked(), blend);
    for (const QString& uid : changed)
        if (Object* item = m_overlayItems.value(uid))
            item->setBlend(blend);
    if (changed.isEmpty())
        return;   // picking the mode it already has is not an edit, and not a history step

    emit stackChanged();
    pushOverlays(changed.size() == 1 ? tr("Set blend") : tr("Set blend on %n objects", "", int(changed.size())));
}

void ObjectController::moveSelectedInStack(bool forward)
{
    if (m_selection.objects().size() != 1 || !m_selection.carriers().isEmpty())
        return;
    const QString uid = m_selection.objects().first();

    if (!m_document.move(uid, forward))
        return;   // already at the end of the stack; nothing to report and nothing to undo
    syncItems();    // z-values come from the vector's order
    emit stackChanged();
    pushOverlays(forward ? tr("Bring forward") : tr("Send back"));
}

void ObjectController::setOverlayEnabled(const QString& uid, bool on)
{
    if (!m_document.setEnabled(uid, on))
        return;
    syncItems();
    pushOverlays(on ? tr("Show overlay") : tr("Hide overlay"));
}

void ObjectController::setStackOrder(const QStringList& bottomUp)
{
    // The rows read bottom-up are the composite order: the render draws last-on-top.
    switch (m_document.setOrder(bottomUp)) {
    case Document::Reorder::Refused:
        // A row landed where no overlay row belongs — among the strip's pages, say. Put the rows back as
        // the model has them rather than commit an order that has lost someone's bubble.
        emit stackChanged();
        return;
    case Document::Reorder::Unchanged:
        // Nothing moved — a signal that looked like a drag was not one. Committing anyway would cost a
        // round trip through the owner for a history step that records nothing.
        reselect();   // put back what the drag's take-and-insert unselected
        return;
    case Document::Reorder::Done:
        break;
    }
    syncItems();
    pushOverlays(tr("Reorder overlays"));
    reselect();
}

void ObjectController::duplicateSelectedOverlay()
{
    Object* item = m_overlayItems.value(m_selection.primary());
    if (!item)
        return;

    const Document::Overlay* overlay = m_document.overlay(m_selection.primary());
    if (!overlay)
        return;
    const Document::Overlay& o = *overlay;
    // Routed through the creation channel, not copied into the list here: the library mints the new
    // uid and, because the artwork is byte-identical, its inventory dedups it onto the same file.
    // Offset within the same page, so a duplicate stays on the artwork its original was talking to.
    // The offset is a pixel nudge, so it becomes a fraction like everything else — a duplicate has
    // to land the same distance away whatever width the chapter is being laid out at.
    const double off = m_layout.fraction(k_duplicateOffset);
    m_selectNewOverlay = true;
    if (!item->composition().picture) {
        emit compositionCreated(item->composition(), o.xFrac + off, o.yFrac + off, o.wFrac,
                           QString::fromStdString(o.anchorInputUid));
    } else {
        // Imported artwork has no drawing of ours to re-emit, so the copy goes through the import
        // channel instead: the library hashes the same bytes and dedups the new placement onto the
        // file that is already there. Routing it through creation would have written an SVG of a
        // *default* bubble — which is what it did before this was two types.
        const auto* art = qobject_cast<PictureObject*>(item);
        emit pictureImportRequested(QString::fromStdString(o.assetPath), o.xFrac + off, o.yFrac + off, o.wFrac,
                                    art ? art->pixmap().size() : QSize(), QString::fromStdString(o.anchorInputUid));
    }
}

void ObjectController::pushOverlays(const QString& undoText)
{
    emit overlaysCommitted(m_document.overlays(), m_document.compositions(), undoText);
}

// --- a new object -----------------------------------------------------------

void ObjectController::requestComposition(const Composition& composition, double xFrac, double yFrac, double wFrac,
                                     const QString& anchorInputUid)
{
    m_selectNewOverlay = true;
    emit compositionCreated(composition, xFrac, yFrac, wFrac, anchorInputUid);
}

void ObjectController::requestPicture(const QString& file, double xFrac, double yFrac, double wFrac,
                                      QSize naturalSize, const QString& anchorInputUid)
{
    m_selectNewOverlay = true;
    emit pictureImportRequested(file, xFrac, yFrac, wFrac, naturalSize, anchorInputUid);
}

}  // namespace StripEdit
