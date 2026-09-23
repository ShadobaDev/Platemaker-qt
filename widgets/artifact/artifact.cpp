#include "artifact.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRandomGenerator>

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
// Artifact
// ---------------------------------------------------------------------------

bool Artifact::operator==(const Artifact& o) const
{
    // Group by group, so adding a property to a group cannot quietly fall out of equality: the
    // group's own operator== is the one place that has to know about it.
    return shape == o.shape && artwork == o.artwork && box == o.box && tails == o.tails
        && text == o.text && style == o.style && styleSeed == o.styleSeed && skin == o.skin;
}

ShapeProperties ShapeProperties::from(const Artifact& a) { return a.shape; }
void            ShapeProperties::applyTo(Artifact& a) const { a.shape = *this; }

StyleProperties StyleProperties::from(const Artifact& a) { return a.style; }
void            StyleProperties::applyTo(Artifact& a) const { a.style = *this; }

TextProperties  TextProperties::from(const Artifact& a) { return a.text; }
void            TextProperties::applyTo(Artifact& a) const { a.text = *this; }

TailsProperties TailsProperties::from(const Artifact& a) { return a.tails; }
void            TailsProperties::applyTo(Artifact& a) const { a.tails = *this; }

TailProperties TailProperties::from(const Artifact& a, int index)
{
    TailProperties p;
    p.index = index;
    if (index >= 0 && index < a.tails.items.size())
        p.tail = a.tails.items.at(index);
    return p;
}

void TailProperties::applyTo(Artifact& a) const
{
    if (index >= 0 && index < a.tails.items.size())
        a.tails.items[index] = tail;
}

SkinProperties SkinProperties::from(const Artifact& a)
{
    return a.skin;
}

void SkinProperties::applyTo(Artifact& a) const
{
    a.skin = *this;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

const char* shapeName(Artifact::Shape s)
{
    // A switch with no default: adding a Shape without naming it here is a compiler warning, not a
    // silently mis-saved bubble.
    switch (s) {
    case Artifact::Shape::None:      return "none";
    case Artifact::Shape::Speech:    return "speech";
    case Artifact::Shape::Shout:     return "shout";
    case Artifact::Shape::Caption:   return "caption";
    case Artifact::Shape::Ellipse:   return "ellipse";
    case Artifact::Shape::Diamond:   return "diamond";
    case Artifact::Shape::Trapezoid: return "trapezoid";
    case Artifact::Shape::Thought:   return "thought";
    case Artifact::Shape::Scroll:    return "scroll";
    case Artifact::Shape::Banner:    return "banner";
    }
    return "speech";
}

QString shapeTitle(Artifact::Shape s)
{
    // A switch with no default, for the same reason shapeName() has none: a new shape must be named
    // here too, and the compiler is what says so.
    switch (s) {
    case Artifact::Shape::None:      return QObject::tr("Text only — no balloon");
    case Artifact::Shape::Speech:    return QObject::tr("Speech balloon");
    case Artifact::Shape::Shout:     return QObject::tr("Shout");
    case Artifact::Shape::Caption:   return QObject::tr("Caption box");
    case Artifact::Shape::Ellipse:   return QObject::tr("Round balloon");
    case Artifact::Shape::Diamond:   return QObject::tr("Diamond");
    case Artifact::Shape::Trapezoid: return QObject::tr("Caption plate");
    case Artifact::Shape::Thought:   return QObject::tr("Thought balloon");
    case Artifact::Shape::Scroll:    return QObject::tr("Scroll");
    case Artifact::Shape::Banner:    return QObject::tr("Banner");
    }
    return {};
}

const QList<Artifact::Shape>& shapeOrder()
{
    using Shape = Artifact::Shape;
    static const QList<Shape> order{
        Shape::Speech, Shape::Ellipse, Shape::Thought,  Shape::Shout,  Shape::Caption,
        Shape::Trapezoid, Shape::Diamond, Shape::Banner, Shape::Scroll,
    };   // Shape::None is not a silhouette — see the header
    return order;
}

Artifact::Shape shapeFromName(QStringView name)
{
    for (int i = 0; i <= int(Artifact::Shape::Banner); ++i) {
        const auto s = static_cast<Artifact::Shape>(i);
        if (name == QLatin1String(shapeName(s)))
            return s;
    }
    return Artifact::Shape::Speech;
}

const char* styleName(Artifact::Style s)
{
    switch (s) {
    case Artifact::Style::Clean:  return "clean";
    case Artifact::Style::Marker: return "marker";
    case Artifact::Style::Ink:    return "ink";
    }
    return "clean";
}

Artifact::Style styleFromName(QStringView name)
{
    for (int i = 0; i <= int(Artifact::Style::Ink); ++i) {
        const auto s = static_cast<Artifact::Style>(i);
        if (name == QLatin1String(styleName(s)))
            return s;
    }
    return Artifact::Style::Clean;
}

QJsonObject artifactToJson(const Artifact& a)
{
    return QJsonObject{
        {QStringLiteral("shape"),      QLatin1String(shapeName(a.shape.kind))},
        {QStringLiteral("artwork"),    a.artwork},
        {QStringLiteral("w"),          a.box.width()},
        {QStringLiteral("h"),          a.box.height()},
        {QStringLiteral("tails"),      tailsToJson(a.tails.items)},
        {QStringLiteral("text"),       a.text.body},
        {QStringLiteral("fontFamily"), a.text.family},
        {QStringLiteral("fontSize"),   a.text.pixelSize},
        {QStringLiteral("bold"),       a.text.bold},
        {QStringLiteral("align"),      a.text.align},
        {QStringLiteral("fill"),       a.skin.fill.name(QColor::HexArgb)},
        {QStringLiteral("stroke"),     a.skin.stroke.name(QColor::HexArgb)},
        {QStringLiteral("textColour"), a.text.colour.name(QColor::HexArgb)},
        {QStringLiteral("strokeWidth"),a.skin.strokeWidth},
        {QStringLiteral("style"),      QLatin1String(styleName(a.style.kind))},
        {QStringLiteral("styleAmount"),a.style.amount},
        {QStringLiteral("styleSeed"),  double(a.styleSeed)},
    };
}

Artifact artifactFromJson(const QJsonObject& j)
{
    Artifact a;

    a.shape.kind = shapeFromName(j.value(QStringLiteral("shape")).toString());
    a.artwork    = j.value(QStringLiteral("artwork")).toString();   // absent → an object we draw

    // Every field is read defensively with the struct's own default as the fallback, so a snapshot
    // written by an older build loads as a usable bubble rather than a blank.
    a.box  = QSize(j.value(QStringLiteral("w")).toInt(a.box.width()),
                   j.value(QStringLiteral("h")).toInt(a.box.height()));
    a.tails.items = tailsFromJson(j.value(QStringLiteral("tails")).toArray());
    a.text.body          = j.value(QStringLiteral("text")).toString();
    a.text.family    = j.value(QStringLiteral("fontFamily")).toString();
    a.text.pixelSize = j.value(QStringLiteral("fontSize")).toInt(a.text.pixelSize);
    a.text.bold          = j.value(QStringLiteral("bold")).toBool(a.text.bold);
    a.text.align         = j.value(QStringLiteral("align")).toInt(a.text.align);
    a.skin.strokeWidth = j.value(QStringLiteral("strokeWidth")).toInt(a.skin.strokeWidth);
    a.skin.fill     = colourFromJson(j, "fill",       a.skin.fill);
    a.skin.stroke   = colourFromJson(j, "stroke",     a.skin.stroke);
    a.text.colour    = colourFromJson(j, "textColour", a.text.colour);
    a.style.kind         = styleFromName(j.value(QStringLiteral("style")).toString());
    a.style.amount   = j.value(QStringLiteral("styleAmount")).toDouble(a.style.amount);
    a.styleSeed     = quint32(j.value(QStringLiteral("styleSeed")).toDouble(0));
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

void topUpStyleSeed(Artifact& a)
{
    // A bubble authored before styles existed carries seed 0, and so would every other one — style a
    // page of them and they would all wear the same wobble. Give it one the first time it is styled.
    if (a.style.kind != Artifact::Style::Clean && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();
}
