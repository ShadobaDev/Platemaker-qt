#include "toolregistry.h"

namespace StripEdit {

const QList<Tool>& tools()
{
    // The rail, in order. Bubble, Text and Caption name the same options page because they author the
    // same object — one panel, so two copies of it cannot drift — and differ only in the shape they
    // place. That is the whole cost of a tool: this row.
    static const QList<Tool> table = {
        {QStringLiteral("select"),
         QStringLiteral(":/icons/tools/pan.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Pan / select (default)"),
         ToolKind::Select, std::nullopt, QString()},

        {QStringLiteral("grade"),
         QStringLiteral(":/icons/tools/cc.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Colour correction"),
         ToolKind::Grade, std::nullopt, QStringLiteral("grade")},

        {QStringLiteral("colour"),
         QStringLiteral(":/icons/tools/bucket.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool",
                           "Paint what you click — the lettering, the outline or the fill. Shift uses "
                           "the secondary colour; X swaps the pair"),
         ToolKind::Apply, std::nullopt, QString()},

        {QStringLiteral("eyedropper"),
         QStringLiteral(":/icons/tools/eyedropper.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Pick a colour from the strip — Ctrl for the secondary"),
         ToolKind::Sample, std::nullopt, QString()},

        {QStringLiteral("bubble"),
         QStringLiteral(":/icons/tools/bubble.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Speech bubble"),
         ToolKind::Create, std::nullopt, QStringLiteral("artifact")},

        {QStringLiteral("text"),
         QStringLiteral(":/icons/tools/text.svg"),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Text"),
         ToolKind::Create, TextArtifact::Shape::None, QStringLiteral("artifact")},

        // No icon file: a tool that places one shape is drawn by the rasteriser that draws that shape,
        // so the button cannot misrepresent what pressing it gives you — and a new shape tool costs no
        // artwork.
        {QStringLiteral("caption"),
         QString(),
         QT_TRANSLATE_NOOP("StripEdit::Tool", "Caption box"),
         ToolKind::Create, TextArtifact::Shape::Caption, QStringLiteral("artifact")},
    };
    return table;
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
