#include "textartifact.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

namespace {

QColor colourFromJson(const QJsonObject& j, const char* key, QColor fallback)
{
    const QString s = j.value(QLatin1String(key)).toString();
    const QColor  c(s);
    return c.isValid() ? c : fallback;
}

QJsonArray tailsToJson(const QList<Tail>& tails)
{
    QJsonArray arr;
    for (const Tail& t : tails)
        arr.append(QJsonObject{
            {QStringLiteral("x"),     t.tip.x()},
            {QStringLiteral("y"),     t.tip.y()},
            {QStringLiteral("width"), t.baseWidth},
            {QStringLiteral("bend"),  t.bend},
        });
    return arr;
}

QList<Tail> tailsFromJson(const QJsonArray& arr)
{
    QList<Tail> tails;
    tails.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Tail t;
        t.tip       = QPointF(o.value(QStringLiteral("x")).toDouble(),
                              o.value(QStringLiteral("y")).toDouble());
        t.baseWidth = o.value(QStringLiteral("width")).toDouble(t.baseWidth);
        t.bend      = o.value(QStringLiteral("bend")).toDouble(t.bend);
        tails.append(t);
    }
    return tails;
}

} // namespace

// ---------------------------------------------------------------------------
// TextArtifact
// ---------------------------------------------------------------------------

bool TextArtifact::operator==(const TextArtifact& o) const
{
    return shape == o.shape && box == o.box && tails == o.tails && text == o.text
        && fontFamily == o.fontFamily && fontPixelSize == o.fontPixelSize && bold == o.bold
        && align == o.align && fill == o.fill && stroke == o.stroke && textColour == o.textColour
        && strokeWidth == o.strokeWidth;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

const char* shapeName(TextArtifact::Shape s)
{
    // A switch with no default: adding a Shape without naming it here is a compiler warning, not a
    // silently mis-saved bubble.
    switch (s) {
    case TextArtifact::Shape::None:      return "none";
    case TextArtifact::Shape::Speech:    return "speech";
    case TextArtifact::Shape::Shout:     return "shout";
    case TextArtifact::Shape::Caption:   return "caption";
    case TextArtifact::Shape::Ellipse:   return "ellipse";
    case TextArtifact::Shape::Diamond:   return "diamond";
    case TextArtifact::Shape::Trapezoid: return "trapezoid";
    case TextArtifact::Shape::Thought:   return "thought";
    case TextArtifact::Shape::Scroll:    return "scroll";
    case TextArtifact::Shape::Banner:    return "banner";
    }
    return "speech";
}

TextArtifact::Shape shapeFromName(QStringView name)
{
    for (int i = 0; i <= int(TextArtifact::Shape::Banner); ++i) {
        const auto s = static_cast<TextArtifact::Shape>(i);
        if (name == QLatin1String(shapeName(s)))
            return s;
    }
    return TextArtifact::Shape::Speech;
}

QJsonObject artifactToJson(const TextArtifact& a)
{
    return QJsonObject{
        {QStringLiteral("shape"),      QLatin1String(shapeName(a.shape))},
        {QStringLiteral("w"),          a.box.width()},
        {QStringLiteral("h"),          a.box.height()},
        {QStringLiteral("tails"),      tailsToJson(a.tails)},
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

    a.shape = shapeFromName(j.value(QStringLiteral("shape")).toString());

    // Every field is read defensively with the struct's own default as the fallback, so a snapshot
    // written by an older build loads as a usable bubble rather than a blank.
    a.box  = QSize(j.value(QStringLiteral("w")).toInt(a.box.width()),
                   j.value(QStringLiteral("h")).toInt(a.box.height()));
    a.tails = tailsFromJson(j.value(QStringLiteral("tails")).toArray());
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
