#ifndef STRIPEDIT_PRESETSTORE_HPP
#define STRIPEDIT_PRESETSTORE_HPP

#include <QList>
#include <QObject>
#include <QString>

#include "artifact.hpp"

namespace StripEdit {

/**
 * @brief A named look, with nothing said in it: shape, colours, stroke, line style, font.
 *
 * Everything a bubble *is*, minus everything it *says* — no text, no box size, no tails, no placement,
 * no style seed. That split is the whole idea: applying one restyles an object without touching the
 * lettering, and picking one sets what the next placement starts from.
 */
struct BubblePreset
{
    QString      name;      //!< What the artist calls this look, which is what they pick it by.
    Artifact artifact;      //!< The look itself, with lettering, box and tails cleared out.
};

/**
 * @brief The preset library: the built-ins, the artist's own, and the file a pack travels in.
 *
 * A store rather than a field on a panel, because presets are wanted in more than one place — the tool's
 * options pick one for the next object, an object's context menu applies one to what is selected, and a
 * dialog will eventually import and export them. A model reachable only by going through a widget is a
 * model that cannot be reached.
 *
 * It lives in the **application config**, not the workspace: restyling is a habit of the artist rather
 * than a property of one comic. A *pack* — the same JSON, in a file — is how one travels to someone else.
 *
 * It raises no dialogs and owns no widgets. Reading and writing a pack report what happened and leave
 * the telling to whoever asked.
 */
class PresetStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a new preset store.
     * @param parent  The parent object.
     */
    explicit PresetStore(QObject* parent = nullptr);

    /**
     * @brief Returns the list of all presets.
     * @return The list of presets.
     */
    [[nodiscard]] const QList<BubblePreset>& presets() const { return m_presets; }
    /**
     * @brief Returns the number of built-in presets.
     * @return The number of built-in presets.
     */
    [[nodiscard]] int builtinCount() const { return m_builtinCount; }
    /**
     * @brief Returns whether the preset at the given index is custom.
     * @param index  The index of the preset.
     * @return True if the preset is custom, false otherwise.
     */
    [[nodiscard]] bool isCustom(int index) const;

    /**
     * @brief Stores \p look under \p name, replacing the artist's own entry of that name.
     *
     * A built-in is not replaceable, so saving over its name keeps both: the artist's own wins by
     * sitting below it, and the built-in is still there to go back to.
     *
     * @param name  The name the artist gave this preset, which is what they pick it by.
     * @param look  The look to store, with lettering, box and tails cleared out
     * @param replaceExisting  False stops at an existing custom entry instead of overwriting it, so
     *                         the caller can ask first. \p index reports where it was.
     * @param existingIndex  If not null, receives the index of an existing entry with the same name.
     * @return The entry's index, or -1 when it stopped.
     */
    int save(const QString& name, const Artifact& look, bool replaceExisting, int* existingIndex = nullptr);

    void remove(int index); //!< Removes one of the artist's own. Built-ins are ignored.

    /**
     * @brief Reads a pack, replacing same-named entries rather than accumulating duplicates.
     * @param path  The path to the pack file.
     * @param error  If not null, receives the error message if the file is not a pack.
     * @return How many arrived, or -1 if the file is not a pack (\p error says which).
     */
    int importPack(const QString& path, QString* error);

    /**
     * @brief Writes the artist's own presets to \p path.
     * @param path  The path to the output file.
     * @param error  If not null, receives the error message on failure.
     * @return True on success, false otherwise.
     */
    bool exportPack(const QString& path, QString* error) const;

    /**
     * @brief Which preset @p a currently looks like, or -1 for none of them.
     *
     * **Computed, never stored.** A remembered "this came from Shout" is a field that goes stale the
     * moment a property is edited, and it cannot answer for a selection of several; comparing the
     * values each time costs nothing, self-heals when the artist edits back to an exact match, and has
     * an honest answer either way.
     *
     * Only the groups a preset *fills* are compared — shape, fill & outline, line style, text style.
     * The box, the tails and the lettering are the object's own and `applied()` copies them across, and
     * the style seed belongs to no group at all: it would make an object that matches in every visible
     * way report as something else.
     * 
     * @param a  The artifact to check against the presets.
     * @return The index of the matching preset, or -1 if none match.
     */
    [[nodiscard]] int matching(const Artifact& a) const;

    /**
     * @brief What to call that look: the preset's name, or *Custom* when it is nobody's.
     * @param a  The artifact to label.
     * @return The label for the artifact.
     */
    [[nodiscard]] QString lookLabel(const Artifact& a) const;

    /**
     * @brief \p target restyled by \p p — the one place that knows what a preset does *not* carry.
     *
     * The lettering, the balloon's size, where its tails point and its own style seed all survive:
     * a preset is a look, not a line, and re-rolling a placed bubble's seed would make its outline
     * jump for a reason nobody asked for.
     *
     * @param p  The preset to apply.
     * @param target  The artifact to restyle.
     * @param keepShape  True where the shape picker is not on screen, so a preset cannot change a
     *                   shape the artist can neither see nor put back.
     */
    [[nodiscard]] static Artifact applied(const BubblePreset& p, const Artifact& target,
                                              bool keepShape);

signals:
    void changed();     //!< The list changed — a picker showing it should rebuild.

private:
    void load();        //!< Loads the artist's own presets from QSettings, after the built-ins are in place.
    void persist();     //!< Writes the artist's own presets to QSettings, after the built-ins are in place.

    QList<BubblePreset> m_presets;          //!< Built-ins first, then the artist's own.
    int                 m_builtinCount = 0; //!< How many of the first entries are built-in, so the rest are the artist's own.
};

}  // namespace StripEdit

#endif // STRIPEDIT_PRESETSTORE_HPP
