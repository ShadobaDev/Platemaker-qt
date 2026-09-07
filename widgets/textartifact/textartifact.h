#ifndef TEXTARTIFACT_H
#define TEXTARTIFACT_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QJsonObject>
#include <QPoint>
#include <QSize>
#include <QString>

/**
 * @brief What a bubble *is* — the editable source a strip overlay is rendered from.
 *
 * The library rasterises artwork and deliberately never grows a text engine, so everything about a
 * bubble's content lives here, on the GUI side. This struct is the *working* form; the SVG in
 * `<workspace>/overlays/` is the stored one, and it carries these same values in a private namespace
 * so a bubble can be re-solved from the file it renders from (see artifactsvg.h).
 *
 * Coordinates are **strip-scale pixels**, the same scale the render composites at, so the scene preview
 * and the baked output are the same geometry by construction (see artifactpainter.h).
 *
 * One struct serves both rail tools: the Text tool is this with `shape == Shape::None`. Two entry
 * points, one object — so a caption can grow a balloon later without changing type, and there is one
 * rasteriser, one schema and one list.
 */
/**
 * @brief One tail: where it points, how wide it leaves the balloon, and how much it curves.
 *
 * The tip is in balloon coordinates and **may fall outside the balloon** — that is the whole point of a
 * tail, and it is why the artifact's drawn extent is computed rather than assumed (artifactBounds()).
 * Length is not stored: it is the distance from the balloon to the tip, so aiming and lengthening are
 * one gesture.
 */
struct Tail
{
    QPointF tip;                //!< Balloon coordinates; outside the balloon is normal.
    qreal   baseWidth = 34.0;   //!< Width where it emerges from the silhouette, in balloon pixels.
    qreal   bend      = 0.0;    //!< -1..+1 — sideways offset of the curve, as a fraction of its length.

    [[nodiscard]] bool operator==(const Tail& o) const
    {
        return tip == o.tip
            && qFuzzyCompare(baseWidth, o.baseWidth)
            && qFuzzyCompare(1.0 + bend, 1.0 + o.bend);   // +1 so an exact 0.0 compares equal
    }
    [[nodiscard]] bool operator!=(const Tail& o) const { return !(*this == o); }
};

struct TextArtifact
{
    //! The silhouette drawn behind the text. `None` is the Text tool: letters with no balloon.
    enum class Shape { None, Speech, Shout, Caption };

    Shape shape = Shape::Speech;

    /**
     * @brief The **balloon** — what the author drags, and what text wraps inside.
     *
     * Not the artifact's drawn extent: a tail may reach well outside it, so the size of the rendered
     * artwork comes from artifactBounds(). Until tails could point anywhere these were one rectangle,
     * with a fixed fraction of the height reserved at the bottom for the tail to live in.
     */
    QSize box{280, 160};

    QList<Tail> tails;           //!< Empty = no tail. More than one = one sound, several speakers.

    QString text;
    QString fontFamily;          //!< Empty = the application's default family.
    int     fontPixelSize = 30;  //!< Strip-scale pixels, so it means the same thing in the output.
    bool    bold  = false;
    int     align = Qt::AlignHCenter;   //!< Horizontal alignment of the wrapped text.

    QColor fill{255, 255, 255};
    QColor stroke{20, 20, 20};
    QColor textColour{20, 20, 20};
    int    strokeWidth = 5;

    //! True when a tail should be drawn. A shapeless artifact has nothing to grow a tail from.
    [[nodiscard]] bool hasTail() const { return shape != Shape::None && !tails.isEmpty(); }

    [[nodiscard]] bool operator==(const TextArtifact& o) const;
    [[nodiscard]] bool operator!=(const TextArtifact& o) const { return !(*this == o); }
};

//! Authoring records for one project's overlays, keyed by `StripOverlay::uid`.
using ArtifactMap = QHash<QString, TextArtifact>;

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

[[nodiscard]] QJsonObject artifactToJson(const TextArtifact& a);
[[nodiscard]] TextArtifact artifactFromJson(const QJsonObject& j);

//! A whole map, keyed by overlay uid — the shape the undo snapshot stores.
[[nodiscard]] QJsonObject artifactsToJsonObject(const ArtifactMap& m);
[[nodiscard]] ArtifactMap artifactsFromJsonObject(const QJsonObject& j);

/**
 * @brief Every open project's authoring records, keyed by project uid.
 *
 * A **cache**, not a store: the records live in the overlays' own SVG files (see artifactsvg.h), and
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

#endif // TEXTARTIFACT_H
