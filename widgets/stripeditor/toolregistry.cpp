#include "toolregistry.h"

#include <QCoreApplication>

namespace StripEdit {

const QList<Tool>& tools()
{
    // The rail, in order. **A row is a kind of object, not a value of one of its properties.** Bubble
    // and Text share an options page because they author the same record and differ in the one thing
    // that is structural — whether the object has a silhouette to fill, roughen and grow tails from.
    //
    // *Caption used to be a third row here and was a mistake*: a caption box is one of ten silhouettes,
    // so the rail offered two kinds and one property value as though they were peers. It is picked in
    // the shape tiles like every other silhouette, and a preset places one in a click.
    static const QList<Tool> table = {
        // Select and Pan are two rows because `dragMode` is one property: the rubber band and the hand
        // cannot both own the left button. Splitting them costs a row — which is what the table is for —
        // and leaves each tool honest about what a drag does.
        {QStringLiteral("select"),
         QStringLiteral(":/icons/tools/select.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Select"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Drag the strip to select everything\n"
                           "the frame covers. Click an object to\n"
                           "take it, Ctrl+click to add it to the\n"
                           "selection. This is the default state."),
         ToolKind::Select, std::nullopt, QString(),
         CursorStyle::Arrow, CursorStyle::Move},

        {QStringLiteral("pan"),
         QStringLiteral(":/icons/tools/pan.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Pan"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Drag the strip to scroll it or hold\n"
                           "the middle button, which pans under \n"
                           "every tool. Dragging an object still\n" 
                           "moves the object."),
         // The hand is the view's own drag mode talking; saying it here too is what stops the two from
         // disagreeing. Over an object the left button still moves it, so the cursor says so.
         ToolKind::Pan, std::nullopt, QString(),
         CursorStyle::Hand, CursorStyle::Move},

        {QStringLiteral("grade"),
         QStringLiteral(":/icons/tools/cc.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Colour correction"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Grades the whole strip. The adjustments\n"
                           "are listed below, in the order the render\n"
                           "applies them."),
         ToolKind::Grade, std::nullopt, QStringLiteral("grade"),
         CursorStyle::Arrow, CursorStyle::Inherit},   // it acts on the strip, not on what you point at

        {QStringLiteral("colour"),
         QStringLiteral(":/icons/tools/bucket.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Bucket paint"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Click a balloon, its outline or\n" 
                           "its lettering to paint whatever is under\n"
                           "the pointer with the primary colour.\n" 
                           "Shift+click spends the secondary.\n"
                           "X swaps the pair, D resets it."),
         ToolKind::Apply, std::nullopt, QString(),
         CursorStyle::Cross, CursorStyle::Inherit},

        {QStringLiteral("eyedropper"),
         QStringLiteral(":/icons/tools/eyedropper.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Colour picker"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Click anywhere on the strip to\n" 
                           "take that colour — the artwork, a balloon\n"
                           "or its lettering, whatever is drawn there.\n"
                           "Right-click takes it into the secondary."),
         ToolKind::Sample, std::nullopt, QString(),
         CursorStyle::Cross, CursorStyle::Inherit},

        {QStringLiteral("bubble"),
         QStringLiteral(":/icons/tools/bubble.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Speech bubble"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", 
                           "Drag on the strip to place\n"
                           "a balloon in the shape below."),
         ToolKind::Create, std::nullopt, QStringLiteral("artifact"),
         CursorStyle::Cross, CursorStyle::Inherit},

        // The third kind of object, and the reason this tool exists: artwork could only be added from
        // the object list's context menu, which is not where anyone looks to *add* something.
        {QStringLiteral("artwork"),
         QStringLiteral(":/icons/tools/artwork.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Artwork"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Drag the picture below onto the strip and it lands at\n"
                           "its own size — past the edge too, if that is the point\n"
                           "of it. Drag on the strip instead and it is fitted to\n"
                           "the box you draw. Original size and Fit to strip width\n"
                           "are on the object's own menu."),
         ToolKind::Create, std::nullopt, QStringLiteral("artwork"),
         CursorStyle::Cross, CursorStyle::Inherit},

        {QStringLiteral("text"),
         QStringLiteral(":/icons/tools/text.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Text"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", 
                           "Drag on the strip to place lettering\n"
                           "with no balloon around it."),
         ToolKind::Create, TextArtifact::Shape::None, QStringLiteral("artifact"),
         CursorStyle::Cross, CursorStyle::Inherit},

    };
    return table;
}

QString toolName(const Tool& t)
{
    return t.name ? QCoreApplication::translate("StripEdit::Tool", t.name) : QString();
}

QString toolHint(const Tool& t)
{
    return t.hint ? QCoreApplication::translate("StripEdit::Tool", t.hint) : QString();
}

QString toolTooltip(const Tool& t)
{
    const QString hint = toolHint(t);
    return hint.isEmpty() ? toolName(t) : QStringLiteral("%1 — %2").arg(toolName(t), hint);
}

const Tool* toolById(const QString& id)
{
    for (const Tool& t : tools())
        if (t.id == id)
            return &t;
    return nullptr;
}

int toolIndex(const QString& id)
{
    for (int i = 0; i < tools().size(); ++i)
        if (tools().at(i).id == id)
            return i;
    return -1;
}

}  // namespace StripEdit
