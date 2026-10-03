#include "canvas/canvasinput.hpp"

#include "objects/objectcontroller.hpp"
#include "toolrail/cursors.hpp"
#include "toolrail/toolregistry.hpp"

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QGraphicsView>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>

namespace StripEdit {

CanvasInput::CanvasInput(QGraphicsView* view, ObjectController* objects, QObject* parent)
    : QObject(parent)
    , m_view(view)
    , m_objects(objects)
{
    m_view->viewport()->installEventFilter(this);   // Ctrl+wheel zoom, and the pointer's own answer
}

void CanvasInput::setTool(const Tool& tool)
{
    m_tool = &tool;
}

void CanvasInput::updateCursor()
{
    const Tool* tool = m_tool;
    if (!tool || !m_view || !m_view->viewport())
        return;
    PointerTarget target = PointerTarget::BareStrip;
    if (m_objects && m_pointerPos.x() >= 0)
        target = m_objects->pointerTargetAt(m_view->mapToScene(m_pointerPos), m_view->transform());
    m_view->viewport()->setCursor(cursorFor(*tool, target));
}

bool CanvasInput::eventFilter(QObject *watched, QEvent *event)
{
    const ToolKind kind = m_tool ? m_tool->kind : ToolKind::Select;

    // **A picture dropped on the strip is placed where it was dropped, at its own size.** Dragged out
    // of the TOOL OPTIONS preview, or straight from a file manager — both arrive as a file URL, so one
    // handler serves both and neither needs a tool to be armed.
    if (watched == m_view->viewport()
        && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto* de = static_cast<QDragMoveEvent*>(event);
        if (!droppedArtwork(de->mimeData()).isEmpty()) {
            de->setDropAction(Qt::CopyAction);
            de->accept();
            return true;
        }
    }
    if (watched == m_view->viewport() && event->type() == QEvent::Drop) {
        auto*         drop = static_cast<QDropEvent*>(event);
        const QString file = droppedArtwork(drop->mimeData());
        if (!file.isEmpty()) {
            emit artworkDropped(file, m_view->mapToScene(drop->position().toPoint()));
            drop->acceptProposedAction();
            return true;
        }
    }

    // The middle button scrolls the strip under **every** tool, so no tool has to give up its left
    // button for something as ordinary as looking somewhere else. Qt's own hand-drag is the left
    // button's, and only the Pan tool arms it.
    if (watched == m_view->viewport()) {
        auto* me = event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove
                           || event->type() == QEvent::MouseButtonRelease
                       ? static_cast<QMouseEvent*>(event)
                       : nullptr;
        if (me && event->type() == QEvent::MouseButtonPress && me->button() == Qt::MiddleButton) {
            m_panFrom = me->position().toPoint();
            m_view->viewport()->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (me && event->type() == QEvent::MouseMove && m_panFrom.x() >= 0) {
            const QPoint now  = me->position().toPoint();
            const QPoint step = now - m_panFrom;
            m_panFrom         = now;
            // Scrollbars take whole steps, so this follows the mouse rather than a remembered origin:
            // there is no fraction left over to drift with.
            m_view->horizontalScrollBar()->setValue(m_view->horizontalScrollBar()->value() - step.x());
            m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->value() - step.y());
            return true;
        }
        if (me && event->type() == QEvent::MouseButtonRelease && me->button() == Qt::MiddleButton) {
            m_panFrom = {-1, -1};
            updateCursor();
            return true;
        }
    }

    // The eyedropper: a press takes the colour that is on the strip there, wherever it lands — over an
    // object as much as over a page, because what is sampled is what is drawn.
    if (watched == m_view->viewport() && kind == ToolKind::Sample) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            // Left fills the primary half, right the secondary — as every eyedropper does. Ctrl+left
            // does the same as right, for a tablet with one barrel button bound to nothing.
            const bool left  = me->button() == Qt::LeftButton;
            const bool right = me->button() == Qt::RightButton;
            if (left || right) {
                emit sampleRequested(m_view->mapToScene(me->position().toPoint()),
                                     right || (me->modifiers() & Qt::ControlModifier));
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            return true;   // the right button is the tool's here, so it opens no menu
        }
    }

    // The colour tool: a press spends the pair on **what is under the pointer** — the lettering, the
    // outline or the fill, decided by the picture rather than by a setting. Shift spends the other half.
    //
    // The left button only. The right one belongs to the context menu, and a tool that quietly took it
    // away would be a mode nobody can see — the same mistake the Text tool made when it stripped the
    // object panel.
    if (watched == m_view->viewport() && kind == ToolKind::Apply
        && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        // A corner grip and a tail tip belong to the canvas under every tool, and the cursor says so —
        // so a press there resizes or aims rather than painting. The tool gets everything else.
        const bool affordance = isCanvasAffordance(
            m_objects->pointerTargetAt(m_view->mapToScene(me->position().toPoint()),
                                       m_view->transform()));
        if (me->button() == Qt::LeftButton && !affordance) {
            const bool other = me->modifiers() & Qt::ShiftModifier;
            emit colourApplyRequested(m_view->mapToScene(me->position().toPoint()), other);
            return true;
        }
    }

    // Bubble / Text: the left button draws a new bubble on empty strip. A press that lands on an
    // existing overlay is left alone, so the item's own move/resize handling still runs.
    if (watched == m_view->viewport() && kind == ToolKind::Create) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const QPointF scenePos = m_view->mapToScene(me->position().toPoint());
                if (!m_objects->objectAt(scenePos, m_view->transform())) {
                    m_objects->beginPlacement(scenePos);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove && m_objects->isPlacing()) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_objects->updatePlacement(m_view->mapToScene(me->position().toPoint()));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_objects->isPlacing()) {
            m_objects->finishPlacement();
            return true;
        }
    }

    if (watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_pointerPos = me->position().toPoint();
            // Only while nothing is held: mid-drag the view writes a closed hand, and an object being
            // dragged is not "what the pointer is over" in any useful sense.
            if (me->buttons() == Qt::NoButton)
                updateCursor();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            // **Queued, and it has to be.** Under ScrollHandDrag the view restores an open hand on every
            // left release — even one that never panned, because the press was taken by an item — and its
            // handler runs after this filter. Deciding here would be overwritten a moment later, which is
            // what made a click on a balloon flash the hand until the mouse moved a pixel. Deciding after
            // the event has been handled puts us last again.
            QMetaObject::invokeMethod(this, [this] { updateCursor(); }, Qt::QueuedConnection);
        } else if (event->type() == QEvent::Enter) {
            m_pointerPos = static_cast<QEnterEvent*>(event)->position().toPoint();
            updateCursor();   // coming back onto the canvas is a hover like any other
        } else if (event->type() == QEvent::Leave) {
            m_pointerPos = {-1, -1};
        }
    }

    if (watched == m_view->viewport() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const int d = we->angleDelta().y();
            if (d > 0)
                emit zoomStepRequested(+1);
            else if (d < 0)
                emit zoomStepRequested(-1);
            return true;        // consumed — plain wheel still scrolls vertically
        }
    }
    return QObject::eventFilter(watched, event);
}

QString CanvasInput::droppedArtwork(const QMimeData* mime)
{
    // A picture, by what it *is* rather than by where it came from: the same three suffixes the import
    // has always taken. Anything else — a page, a workspace, a folder — is not for this canvas.
    if (!mime || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        for (const char* ext : {".svg", ".png", ".webp"})
            if (path.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
                return path;
    }
    return {};
}

}  // namespace StripEdit
