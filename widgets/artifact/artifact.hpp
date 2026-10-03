#ifndef ARTIFACT_HPP
#define ARTIFACT_HPP

#include <QColor>
#include <QHash>
#include <QList>
#include <QStringView>
#include <QtGlobal>
#include <QPointF>
#include <QJsonObject>
#include <QPoint>
#include <QSize>
#include <QString>

#include "propertygroups.hpp"   // the groups an Artifact is made of

/**
 * @brief What a bubble *is* — the editable source a strip overlay is rendered from.
 *
 * The library rasterises artwork and deliberately never grows a text engine, so everything about a
 * bubble's content lives here, on the GUI side. This struct is the *working* form; the SVG in
 * `<workspace>/overlays/` is the stored one, and it carries these same values in a private namespace
 * so a bubble can be re-solved from the file it renders from (see artifactsvg.hpp).
 *
 * Coordinates are **strip-scale pixels**, the same scale the render composites at, so the scene preview
 * and the baked output are the same geometry by construction (see artifactpainter.hpp).
 *
 * One struct serves both rail tools: the Text tool is this with `shape == Shape::None`. Two entry
 * points, one object — so a caption can grow a balloon later without changing type, and there is one
 * rasteriser, one schema and one list.
 */
struct Artifact
{
    //! The enums live with the groups that own them; these keep every existing spelling working.
    using Shape = ShapeProperties::Kind;
    using Style = StyleProperties::Kind;

    ShapeProperties shape;  //<! What silhouette is drawn behind the text, if any.

    /**
     * @brief The imported picture this object **is** — a file name in the workspace's `overlays/`.
     *
     * Empty for an object we draw ourselves. Non-empty and the drawing is somebody else's: our
     * silhouette, our line style and our tails have nothing to act on, and what is left that we can
     * still do is put lettering over it and decide how big it is drawn.
     *
     * **This is the third kind, and it lives in the same record on purpose.** A second map keyed by the
     * same uid would be a second channel carrying the same object, and the two would drift; a record
     * that says what it is keeps one. A *file name* rather than a path so the workspace stays portable,
     * and a name inside `overlays/` rather than the import's source so it stays self-contained — the
     * source may be anywhere, or gone.
     */
    QString artwork;

    /**
     * @brief The **balloon** — what the author drags, and what text wraps inside.
     *
     * Not the artifact's drawn extent: a tail may reach well outside it, so the size of the rendered
     * artwork comes from artifactBounds(). Until tails could point anywhere these were one rectangle,
     * with a fixed fraction of the height reserved at the bottom for the tail to live in.
     */
    QSize box{280, 160};

    TailsProperties tails;
    StyleProperties style;

    /**
     * @brief Seeds the filter's noise, so two bubbles do not wear identical wobble.
     *
     * Stored rather than derived, because it must survive an edit: re-rolling it on every keystroke
     * would make the outline crawl while you type. Set once, when the bubble is placed.
     */
    quint32 styleSeed   = 0;

    TextProperties text;
    SkinProperties skin;   //!< Fill, stroke and stroke width — see SkinProperties.

    //! Somebody else drew this one: the drawing is `artwork`, and none of our geometry applies to it.
    [[nodiscard]] bool isArtwork() const { return !artwork.isEmpty(); }

    /**
     * @brief Whether this object has a balloon behind its lettering — **the one structural question**.
     *
     * It decides which property groups the object carries at all: a fill and an outline to paint, a
     * line style to roughen them with, and something for a tail to leave from. Everything else about
     * `shape` — speech, thought, caption, banner — is one property with one editor, which is why
     * *which* silhouette is picked in a panel and *whether there is one* is `Convert to ▸`.
     *
     * Written out by hand in twenty places before it had a name, in both polarities.
     */
    [[nodiscard]] bool hasSilhouette() const { return !isArtwork() && shape.kind != Shape::None; }

    //! True when a tail should be drawn. A shapeless artifact has nothing to grow a tail from.
    [[nodiscard]] bool hasTail() const { return hasSilhouette() && !tails.items.isEmpty(); }

    [[nodiscard]] bool operator==(const Artifact& o) const;
    [[nodiscard]] bool operator!=(const Artifact& o) const { return !(*this == o); }
};

/**
 * @brief Gives \p a a style seed if it is styled and has none — **the one place that mints one**.
 *
 * The seed belongs to no property group, precisely so that no editor and no preset can copy it: a page
 * of marker balloons all wearing the same wobble is the failure it exists to prevent. That makes *who
 * mints it* a question with exactly one right answer, and it is here — four callers had each written
 * the rule out, and one of them had already diverged into minting a seed for balloons that have no
 * style to wobble.
 *
 * Idempotent: a record that already has a seed keeps it, so re-styling never re-rolls the wobble.
 */
void topUpStyleSeed(Artifact& a);

//! Authoring records for one project's overlays, keyed by `StripOverlay::uid`.
using ArtifactMap = QHash<QString, Artifact>;

/**
 * @brief The persisted name of \p s, and the way back.
 *
 * One mapping, used by both persistence paths — the undo snapshot's JSON and the SVG's `pm:shape`. It
 * used to be a positional array in one and a switch in the other, which meant appending a shape was
 * a silent out-of-bounds read on one side and a compiler error on neither.
 */
[[nodiscard]] const char* shapeName(Artifact::Shape s);

/**
 * @brief What to call \p s on screen, translated — as opposed to shapeName(), which is what it is
 *        called in a file.
 *
 * The two are deliberately separate: a persisted name may never change, and a displayed one must be
 * free to. It lives here rather than in the shape picker because the picker is no longer the only
 * thing that names a shape — *Convert to ▸* does too, and two lists would drift.
 */
[[nodiscard]] QString shapeTitle(Artifact::Shape s);

/**
 * @brief Every **silhouette**, in the order the pickers offer them. Not the enum's order, which is
 *        append-only and therefore historical.
 *
 * `Shape::None` is deliberately **not** in it. It is not a silhouette to choose between; it is the
 * absence of one, which is a different *kind* of object — no fill, no outline, no line style, nothing
 * for a tail to leave from. Choosing it belongs to *Convert to ▸*, and a picker that offered it would
 * let a property control change what the object is.
 */
[[nodiscard]] const QList<Artifact::Shape>& shapeOrder();
//! Parses \p name; anything unrecognised falls back to Speech, so an unknown shape still draws.
[[nodiscard]] Artifact::Shape shapeFromName(QStringView name);

//! The persisted name of \p s, and the way back — same contract as shapeName().
[[nodiscard]] const char* styleName(Artifact::Style s);
[[nodiscard]] Artifact::Style styleFromName(QStringView name);

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

[[nodiscard]] QJsonObject artifactToJson(const Artifact& a);
[[nodiscard]] Artifact artifactFromJson(const QJsonObject& j);

//! A whole map, keyed by overlay uid — the shape the undo snapshot stores.
[[nodiscard]] QJsonObject artifactsToJsonObject(const ArtifactMap& m);
[[nodiscard]] ArtifactMap artifactsFromJsonObject(const QJsonObject& j);

/**
 * @brief Every open project's authoring records, keyed by project uid.
 *
 * A **cache**, not a store: the records live in the overlays' own SVG files (see artifactsvg.hpp), and
 * this holds the parsed form so a populate() does not re-read and re-parse the whole chapter. It is
 * repopulated from disk when a workspace is opened and written through whenever the editor commits.
 *
 * There is deliberately nothing to save here. An authoring sidecar would be a second copy of what the
 * asset already carries — one more file to keep in step, and one more thing to lose separately from the
 * artwork it describes.
 */
class ArtifactStore
{
public:
    //! `<workspace dir>/overlays` — where the SVG assets live; created on demand by ensureDir().
    [[nodiscard]] static QString overlaysDir(const QString& workspacePath);
    //! Creates the overlays directory if missing. Returns its path, or empty if it cannot be created.
    [[nodiscard]] static QString ensureOverlaysDir(const QString& workspacePath);

    void clear() { m_byProject.clear(); }

    [[nodiscard]] ArtifactMap        artifacts(const QString& projectUid) const;
    void                             setArtifacts(const QString& projectUid, ArtifactMap map);

private:
    QHash<QString, ArtifactMap> m_byProject;   //!< project uid → (overlay uid → artifact)
};

#endif // ARTIFACT_HPP
