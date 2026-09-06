#include "textartifact.h"

#include <QDir>
#include <QFileInfo>

namespace {

QColor colourFromJson(const QJsonObject& j, const char* key, QColor fallback)
{
    const QString s = j.value(QLatin1String(key)).toString();
    const QColor  c(s);
    return c.isValid() ? c : fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// TextArtifact
// ---------------------------------------------------------------------------

bool TextArtifact::operator==(const TextArtifact& o) const
{
    return shape == o.shape && box == o.box && tail == o.tail && text == o.text
        && fontFamily == o.fontFamily && fontPixelSize == o.fontPixelSize && bold == o.bold
        && align == o.align && fill == o.fill && stroke == o.stroke && textColour == o.textColour
        && strokeWidth == o.strokeWidth;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

QJsonObject artifactToJson(const TextArtifact& a)
{
    static const char* names[] = {"none", "speech", "shout", "caption"};
    return QJsonObject{
        {QStringLiteral("shape"),      QLatin1String(names[static_cast<int>(a.shape)])},
        {QStringLiteral("w"),          a.box.width()},
        {QStringLiteral("h"),          a.box.height()},
        {QStringLiteral("tailX"),      a.tail.x()},
        {QStringLiteral("tailY"),      a.tail.y()},
        {QStringLiteral("text"),       a.text},
        {QStringLiteral("fontFamily"), a.fontFamily},
        {QStringLiteral("fontSize"),   a.fontPixelSize},
        {QStringLiteral("bold"),       a.bold},
        {QStringLiteral("align"),      a.align},
        {QStringLiteral("fill"),       a.fill.name(QColor::HexArgb)},
        {QStringLiteral("stroke"),     a.stroke.name(QColor::HexArgb)},
        {QStringLiteral("textColour"), a.textColour.name(QColor::HexArgb)},
        {QStringLiteral("strokeWidth"),a.strokeWidth},
    };
}

TextArtifact artifactFromJson(const QJsonObject& j)
{
    TextArtifact a;

    const QString shape = j.value(QStringLiteral("shape")).toString();
    if      (shape == QLatin1String("none"))    a.shape = TextArtifact::Shape::None;
    else if (shape == QLatin1String("shout"))   a.shape = TextArtifact::Shape::Shout;
    else if (shape == QLatin1String("caption")) a.shape = TextArtifact::Shape::Caption;
    else                                        a.shape = TextArtifact::Shape::Speech;

    // Every field is read defensively with the struct's own default as the fallback, so a snapshot
    // written by an older build loads as a usable bubble rather than a blank.
    a.box  = QSize(j.value(QStringLiteral("w")).toInt(a.box.width()),
                   j.value(QStringLiteral("h")).toInt(a.box.height()));
    a.tail = QPoint(j.value(QStringLiteral("tailX")).toInt(a.tail.x()),
                    j.value(QStringLiteral("tailY")).toInt(a.tail.y()));
    a.text          = j.value(QStringLiteral("text")).toString();
    a.fontFamily    = j.value(QStringLiteral("fontFamily")).toString();
    a.fontPixelSize = j.value(QStringLiteral("fontSize")).toInt(a.fontPixelSize);
    a.bold          = j.value(QStringLiteral("bold")).toBool(a.bold);
    a.align         = j.value(QStringLiteral("align")).toInt(a.align);
    a.strokeWidth   = j.value(QStringLiteral("strokeWidth")).toInt(a.strokeWidth);
    a.fill          = colourFromJson(j, "fill",       a.fill);
    a.stroke        = colourFromJson(j, "stroke",     a.stroke);
    a.textColour    = colourFromJson(j, "textColour", a.textColour);
    return a;
}

QJsonObject artifactsToJsonObject(const ArtifactMap& m)
{
    QJsonObject j;
    for (auto it = m.begin(); it != m.end(); ++it)
        j.insert(it.key(), artifactToJson(it.value()));
    return j;
}

ArtifactMap artifactsFromJsonObject(const QJsonObject& j)
{
    ArtifactMap m;
    for (auto it = j.begin(); it != j.end(); ++it)
        m.insert(it.key(), artifactFromJson(it.value().toObject()));
    return m;
}

// ---------------------------------------------------------------------------
// ArtifactStore
// ---------------------------------------------------------------------------

QString ArtifactStore::overlaysDir(const QString& workspacePath)
{
    if (workspacePath.isEmpty())
        return {};
    return QFileInfo(workspacePath).absolutePath() + QStringLiteral("/overlays");
}

QString ArtifactStore::ensureOverlaysDir(const QString& workspacePath)
{
    const QString dir = overlaysDir(workspacePath);
    if (dir.isEmpty())
        return {};
    return QDir().mkpath(dir) ? dir : QString{};
}

ArtifactMap ArtifactStore::artifacts(const QString& projectUid) const
{
    return m_byProject.value(projectUid);
}

void ArtifactStore::setArtifacts(const QString& projectUid, ArtifactMap map)
{
    if (map.isEmpty())
        m_byProject.remove(projectUid);
    else
        m_byProject.insert(projectUid, std::move(map));
}
