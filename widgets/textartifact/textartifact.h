#ifndef TEXTARTIFACT_H
#define TEXTARTIFACT_H

#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QSize>
#include <QString>

class QPainter;

/**
 * @brief What a bubble *is* — the editable source a strip overlay is rendered from.
 *
 * The library composites a flat RGBA bitmap and deliberately never grows a text engine, so everything
 * about a bubble's content lives here, on the GUI side. That split is what makes a bubble re-editable:
 * the PNG in `<workspace>/overlays/` is the *render* artifact, this struct is the *source*. Changing
 * one word re-rasterises from here instead of asking the author to redraw.
 *
 * Coordinates are **strip-scale pixels**, the same scale the render composites at, so the scene preview
 * and the baked output are the same pixels by construction (see renderArtifact()).
 *
 * One struct serves both rail tools: the Text tool is this with `shape == Shape::None`. Two entry
 * points, one object — so a caption can grow a balloon later without changing type, and there is one
 * rasteriser, one schema and one list.
 */
struct TextArtifact
{
    //! The silhouette drawn behind the text. `None` is the Text tool: letters with no balloon.
    enum class Shape { None, Speech, Shout, Caption };

    Shape  shape = Shape::Speech;
    QSize  box{280, 160};        //!< The whole artifact, tail included — this *is* the bitmap's size.
    QPoint tail{70, 158};        //!< Tip, in box coordinates. `y < 0` = no tail (see hasTail()).

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
    [[nodiscard]] bool hasTail() const { return shape != Shape::None && tail.y() >= 0; }

    [[nodiscard]] bool operator==(const TextArtifact& o) const;
    [[nodiscard]] bool operator!=(const TextArtifact& o) const { return !(*this == o); }
};

//! Authoring records for one project's overlays, keyed by `StripOverlay::uid`.
using ArtifactMap = QHash<QString, TextArtifact>;

// ---------------------------------------------------------------------------
// Rasterising — the single definition of what a bubble looks like
// ---------------------------------------------------------------------------

/**
 * @brief Draws \p a into the rectangle (0, 0, a.box), on whatever painter is given.
 *
 * The scene preview and the PNG both go through here, so "what you see" and "what is baked" cannot
 * drift apart — they are the same code path over the same numbers.
 */
void paintArtifact(QPainter& painter, const TextArtifact& a);

//! Rasterises \p a to a transparent ARGB32 image the size of its box — what the library composites.
[[nodiscard]] QImage renderArtifact(const TextArtifact& a);

//! The box height that fits \p a's text at its current width (its width, and a sane floor, are kept).
[[nodiscard]] QSize fittedBox(const TextArtifact& a);

//! Human-readable label for the artifact list — the first line of text, or the shape's name if empty.
[[nodiscard]] QString artifactLabel(const TextArtifact& a);

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

[[nodiscard]] QJsonObject artifactToJson(const TextArtifact& a);
[[nodiscard]] TextArtifact artifactFromJson(const QJsonObject& j);

//! A whole map, keyed by overlay uid — the shape the sidecar and the undo snapshot both store.
[[nodiscard]] QJsonObject artifactsToJsonObject(const ArtifactMap& m);
[[nodiscard]] ArtifactMap artifactsFromJsonObject(const QJsonObject& j);

/**
 * @brief Every project's authoring records, persisted beside the workspace file.
 *
 * A sidecar rather than a field in the workspace JSON, because that codec belongs to the **library**
 * and this is information the library refuses to model. It follows the storage split the rest of the
 * app runs on — the library is a complete, OS-path-agnostic tool; the GUI decides where things live —
 * and it keeps a workspace self-contained: the `.platemaker.json`, `overlays/` and this file sit in one
 * directory and copy together.
 *
 * Deliberately **not** in `.platemaker-cache/`: that directory is regenerable and safe to delete, and
 * losing these records would silently flatten every bubble into un-editable art.
 */
class ArtifactStore
{
public:
    //! `<workspace>.overlays.json` beside the workspace file; empty in, empty out.
    [[nodiscard]] static QString sidecarPath(const QString& workspacePath);
    //! `<workspace dir>/overlays` — where the rasterised PNGs live; created on demand by ensureDir().
    [[nodiscard]] static QString overlaysDir(const QString& workspacePath);
    //! Creates the overlays directory if missing. Returns its path, or empty if it cannot be created.
    [[nodiscard]] static QString ensureOverlaysDir(const QString& workspacePath);

    //! Reads the sidecar for \p workspacePath. A missing or unreadable file just leaves the store empty.
    void load(const QString& workspacePath);
    //! Writes the sidecar. Skipped (and reported false) when there is nothing to write and no file yet.
    bool save(const QString& workspacePath) const;
    void clear() { m_byProject.clear(); }

    [[nodiscard]] ArtifactMap        artifacts(const QString& projectUid) const;
    void                             setArtifacts(const QString& projectUid, ArtifactMap map);

private:
    QHash<QString, ArtifactMap> m_byProject;   //!< project uid → (overlay uid → artifact)
};

#endif // TEXTARTIFACT_H
