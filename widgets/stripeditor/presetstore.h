#ifndef STRIPEDIT_PRESETSTORE_H
#define STRIPEDIT_PRESETSTORE_H

#include <QList>
#include <QObject>
#include <QString>

#include "textartifact.h"

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
    QString      name;
    TextArtifact artifact;
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
    explicit PresetStore(QObject* parent = nullptr);

    [[nodiscard]] const QList<BubblePreset>& presets() const { return m_presets; }
    //! How many of presets() are built in. Those cannot be replaced or removed.
    [[nodiscard]] int builtinCount() const { return m_builtinCount; }
    [[nodiscard]] bool isCustom(int index) const;

    /**
     * @brief Stores \p look under \p name, replacing the artist's own entry of that name.
     *
     * A built-in is not replaceable, so saving over its name keeps both: the artist's own wins by
     * sitting below it, and the built-in is still there to go back to.
     *
     * @param replaceExisting  False stops at an existing custom entry instead of overwriting it, so
     *                         the caller can ask first. \p index reports where it was.
     * @return The entry's index, or -1 when it stopped.
     */
    int save(const QString& name, const TextArtifact& look, bool replaceExisting, int* existingIndex = nullptr);

    //! Removes one of the artist's own. Built-ins are ignored.
    void remove(int index);

    //! Reads a pack, replacing same-named entries rather than accumulating duplicates.
    //! @return How many arrived, or -1 if the file is not a pack (\p error says which).
    int importPack(const QString& path, QString* error);

    //! Writes the artist's own to \p path. @return false and sets \p error on failure.
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
     */
    [[nodiscard]] int matching(const TextArtifact& a) const;

    //! What to call that look: the preset's name, or *Custom* when it is nobody's.
    [[nodiscard]] QString lookLabel(const TextArtifact& a) const;

    /**
     * @brief \p target restyled by \p p — the one place that knows what a preset does *not* carry.
     *
     * The lettering, the balloon's size, where its tails point and its own style seed all survive:
     * a preset is a look, not a line, and re-rolling a placed bubble's seed would make its outline
     * jump for a reason nobody asked for.
     *
     * @param keepShape  True where the shape picker is not on screen, so a preset cannot change a
     *                   shape the artist can neither see nor put back.
     */
    [[nodiscard]] static TextArtifact applied(const BubblePreset& p, const TextArtifact& target,
                                              bool keepShape);

signals:
    //! The list changed — a picker showing it should rebuild.
    void changed();

private:
    void load();
    void persist();

    QList<BubblePreset> m_presets;        //!< Built-ins first, then the artist's own.
    int                 m_builtinCount = 0;
};

}  // namespace StripEdit

#endif // STRIPEDIT_PRESETSTORE_H
