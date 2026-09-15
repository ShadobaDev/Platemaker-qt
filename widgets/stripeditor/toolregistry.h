#ifndef STRIPEDIT_TOOLREGISTRY_H
#define STRIPEDIT_TOOLREGISTRY_H

#include <optional>

#include <QList>
#include <QString>

#include "textartifact.h"

namespace StripEdit {

/**
 * @brief What a tool does to the canvas. There are exactly these, and every tool is one of them.
 *
 * Selecting, moving and dragging a handle are **not** here: they are what the canvas does under every
 * tool, so no tool grants them. `Select` is the default state rather than a tool that permits anything.
 */
enum class ToolKind {
    Select,   //!< The default state: hand-drag pans the view, and the left button is the canvas's.
    Create,   //!< A drag on empty strip places an object.
    Grade,    //!< Edits the selected object's colour; picked with nothing selected, it takes the strip.
    Sample,   //!< A press reads the canvas instead of changing it — the eyedropper.
    Apply,    //!< A press spends the colour pair on the object it lands on.
};

/**
 * @brief One entry on the tool rail — a record, because a tool is stateless.
 *
 * What the editor asks of a tool is a name, an icon, a tooltip, what it does to the canvas and which
 * tool-options page it shows. That is data, so the framework is a table rather than a class hierarchy:
 * a record cannot grow a per-tool copy of the document by accident, and adding a tool cannot mean
 * editing the rail, the options stack, the drag mode and two panels one at a time.
 *
 * The day a tool needs behaviour of its own — the eyedropper's press on the canvas is the first — this
 * record gains one hook. It does not become a class per button.
 */
struct Tool
{
    QString  id;                 //!< Stable name: `setTool()`, settings, tests. Never shown.
    QString  icon;               //!< Resource path, or empty to draw the shape this tool places.
    //! The tooltip, untranslated: a static table is built once, and a language change must not freeze
    //! into it. The rail translates this when it builds the button.
    const char* tip = nullptr;
    ToolKind kind = ToolKind::Select;
    //! `Create` only: the shape a placement gets. No value leaves the choice to the shape tiles in ④,
    //! which is the Bubble tool; `Shape::None` is the Text tool, letters with no balloon.
    std::optional<TextArtifact::Shape> shape;
    //! Which tool-options page: empty for none, `grade`, or `artifact` (what the next object will be).
    QString page;
};

//! Every tool the rail offers, in rail order. **Adding a tool is one row here and nothing else.**
[[nodiscard]] const QList<Tool>& tools();

//! The tool with @p id, or nullptr if there is none.
[[nodiscard]] const Tool* toolById(const QString& id);

//! Its position in tools() — the rail button's id — or -1.
[[nodiscard]] int toolIndex(const QString& id);

}  // namespace StripEdit

#endif // STRIPEDIT_TOOLREGISTRY_H
