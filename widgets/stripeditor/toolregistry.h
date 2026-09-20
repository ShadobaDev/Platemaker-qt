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
    Select,   //!< The default state: a drag on the bare strip draws a rubber band over what it covers.
    Pan,      //!< A drag on the bare strip scrolls the view. The left button belongs to the viewport.
    Create,   //!< A drag on empty strip places an object.
    Grade,    //!< Edits the selected object's colour; picked with nothing selected, it takes the strip.
    Sample,   //!< A press reads the canvas instead of changing it — the eyedropper.
    Apply,    //!< A press spends the colour pair on the object it lands on.
};

/**
 * @brief The cursor a tool asks for. `Inherit` means "whatever this tool shows over the bare strip".
 *
 * A style rather than a `QCursor`, so the table stays data: what a style is drawn as belongs to
 * `cursors.cpp`, which is also where a generated tool-glyph cursor will come from.
 */
enum class CursorStyle { Arrow, Hand, Cross, Move, Inherit };

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
    //! What the rail calls it, untranslated: a static table is built once, and a language change must
    //! not freeze into it. A name, not a sentence — the sentence is below.
    const char* name = nullptr;
    /**
     * @brief One sentence: what a press or a drag with this tool does. Untranslated, as above.
     *
     * **A tool with no options page shows this in ④**, because an empty options panel reads as *nothing
     * is armed* — which is how a tool gets picked by accident and the artist then looks for the fault
     * somewhere else entirely. It is also the second half of the rail button's tooltip, so the sentence
     * is written once rather than once per place it is read; `toolTooltip()` joins the two.
     */
    const char* hint = nullptr;
    ToolKind kind = ToolKind::Select;   //!< What a press or a drag does to the canvas.
    //! `Create` only: the shape a placement gets. No value leaves the choice to the shape tiles in ④,
    //! which is the Bubble tool; `Shape::None` is the Text tool, letters with no balloon.
    std::optional<TextArtifact::Shape> shape;
    //! Which tool-options page: empty for none, `grade`, or `artifact` (what the next object will be).
    QString page;

    //! Over the bare strip. `Hand` is what `ScrollHandDrag` writes anyway, so the two agree.
    CursorStyle cursor = CursorStyle::Arrow;
    //! Over an object's body. `Inherit` keeps the one above — a tool that acts on objects usually should.
    CursorStyle cursorOnObject = CursorStyle::Inherit;
};

//! Every tool the rail offers, in rail order. **Adding a tool is one row here and nothing else.**
[[nodiscard]] const QList<Tool>& tools();

//! What to call the tool, translated.
[[nodiscard]] QString toolName(const Tool& t);

//! Its sentence, translated; empty when the row carries none.
[[nodiscard]] QString toolHint(const Tool& t);

//! The rail button's tooltip: the name, and the sentence after it when there is one.
[[nodiscard]] QString toolTooltip(const Tool& t);

//! The tool with @p id, or nullptr if there is none.
[[nodiscard]] const Tool* toolById(const QString& id);

//! Its position in tools() — the rail button's id — or -1.
[[nodiscard]] int toolIndex(const QString& id);

}  // namespace StripEdit

#endif // STRIPEDIT_TOOLREGISTRY_H
