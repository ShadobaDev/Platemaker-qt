#include "cursors.h"

namespace StripEdit {

namespace {

[[nodiscard]] QCursor stock(CursorStyle style)
{
    switch (style) {
    case CursorStyle::Hand:  return Qt::OpenHandCursor;   // what ScrollHandDrag writes, so we agree
    case CursorStyle::Cross: return Qt::CrossCursor;
    case CursorStyle::Move:  return Qt::SizeAllCursor;
    case CursorStyle::Arrow:
    case CursorStyle::Inherit:
        break;
    }
    return Qt::ArrowCursor;
}

}  // namespace

bool isCanvasAffordance(PointerTarget target)
{
    return target == PointerTarget::ResizeFDiag || target == PointerTarget::ResizeBDiag
        || target == PointerTarget::TailHandle;
}

QCursor cursorFor(const Tool& tool, PointerTarget target)
{
    switch (target) {
    // The canvas's, whatever the tool: a corner resizes and a tail tip aims under every one of them, so
    // a cursor that showed the tool here would promise something a drag does not do.
    case PointerTarget::ResizeFDiag: return Qt::SizeFDiagCursor;
    case PointerTarget::ResizeBDiag: return Qt::SizeBDiagCursor;
    case PointerTarget::TailHandle:  return Qt::CrossCursor;

    // An unanchored object is not on the strip; there is nothing to do to it until it is re-anchored.
    case PointerTarget::Orphan:      return Qt::ArrowCursor;

    case PointerTarget::Object:
        return stock(tool.cursorOnObject == CursorStyle::Inherit ? tool.cursor : tool.cursorOnObject);
    case PointerTarget::BareStrip:
        return stock(tool.cursor);
    }
    return Qt::ArrowCursor;
}

}  // namespace StripEdit
