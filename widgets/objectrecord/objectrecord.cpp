#include "objectrecord.hpp"

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
// ObjectRecord
// ---------------------------------------------------------------------------

bool ObjectRecord::operator==(const ObjectRecord& o) const
{
    // Group by group, so adding a property to a group cannot quietly fall out of equality: the
    // group's own operator== is the one place that has to know about it.
    return shape == o.shape && artwork == o.artwork && box == o.box && tails == o.tails
        && text == o.text && style == o.style && styleSeed == o.styleSeed && skin == o.skin;
}

ShapeProperties ShapeProperties::from(const ObjectRecord& a) { return a.shape; }
void            ShapeProperties::applyTo(ObjectRecord& a) const { a.shape = *this; }

StyleProperties StyleProperties::from(const ObjectRecord& a) { return a.style; }
void            StyleProperties::applyTo(ObjectRecord& a) const { a.style = *this; }

TextProperties  TextProperties::from(const ObjectRecord& a) { return a.text; }
void            TextProperties::applyTo(ObjectRecord& a) const { a.text = *this; }

TailsProperties TailsProperties::from(const ObjectRecord& a) { return a.tails; }
void            TailsProperties::applyTo(ObjectRecord& a) const { a.tails = *this; }

TailProperties TailProperties::from(const ObjectRecord& a, int index)
{
    TailProperties p;
    p.index = index;
    if (index >= 0 && index < a.tails.items.size())
        p.tail = a.tails.items.at(index);
    return p;
}

void TailProperties::applyTo(ObjectRecord& a) const
{
    if (index >= 0 && index < a.tails.items.size())
        a.tails.items[index] = tail;
}

SkinProperties SkinProperties::from(const ObjectRecord& a)
{
    return a.skin;
}

void SkinProperties::applyTo(ObjectRecord& a) const
{
    a.skin = *this;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

const char* shapeName(ObjectRecord::Shape s)
{
    // A switch with no default: adding a Shape without naming it here is a compiler warning, not a
    // silently mis-saved bubble.
    switch (s) {
    case ObjectRecord::Shape::None:      return "none";
    case ObjectRecord::Shape::Speech:    return "speech";
    case ObjectRecord::Shape::Shout:     return "shout";
    case ObjectRecord::Shape::Caption:   return "caption";
    case ObjectRecord::Shape::Ellipse:   return "ellipse";
    case ObjectRecord::Shape::Diamond:   return "diamond";
    case ObjectRecord::Shape::Trapezoid: return "trapezoid";
    case ObjectRecord::Shape::Thought:   return "thought";
    case ObjectRecord::Shape::Scroll:    return "scroll";
    case ObjectRecord::Shape::Banner:    return "banner";
    }
    return "speech";
}

QString shapeTitle(ObjectRecord::Shape s)
{
    // A switch with no default, for the same reason shapeName() has none: a new shape must be named
    // here too, and the compiler is what says so.
    switch (s) {
    case ObjectRecord::Shape::None:      return QObject::tr("Text only — no balloon");
    case ObjectRecord::Shape::Speech:    return QObject::tr("Speech balloon");
    case ObjectRecord::Shape::Shout:     return QObject::tr("Shout");
    case ObjectRecord::Shape::Caption:   return QObject::tr("Caption box");
    case ObjectRecord::Shape::Ellipse:   return QObject::tr("Round balloon");
    case ObjectRecord::Shape::Diamond:   return QObject::tr("Diamond");
    case ObjectRecord::Shape::Trapezoid: return QObject::tr("Caption plate");
    case ObjectRecord::Shape::Thought:   return QObject::tr("Thought balloon");
    case ObjectRecord::Shape::Scroll:    return QObject::tr("Scroll");
    case ObjectRecord::Shape::Banner:    return QObject::tr("Banner");
    }
    return {};
}

const QList<ObjectRecord::Shape>& shapeOrder()
{
    using Shape = ObjectRecord::Shape;
    static const QList<Shape> order{
        Shape::Speech, Shape::Ellipse, Shape::Thought,  Shape::Shout,  Shape::Caption,
        Shape::Trapezoid, Shape::Diamond, Shape::Banner, Shape::Scroll,
    };   // Shape::None is not a silhouette — see the header
    return order;
}

ObjectRecord::Shape shapeFromName(QStringView name)
{
    // The picker's list and the one kind it leaves out — not a bound of our own. A bound written
    // here (it was `<= Banner`) is passed silently by the next shape appended, which then loads from
    // every file as a speech balloon; a shape missing from the picker is caught by the unit tests.
    if (name == QLatin1String(shapeName(ObjectRecord::Shape::None)))
        return ObjectRecord::Shape::None;
    for (ObjectRecord::Shape s : shapeOrder())
        if (name == QLatin1String(shapeName(s)))
            return s;
    return ObjectRecord::Shape::Speech;
}

const char* styleName(ObjectRecord::Style s)
{
    switch (s) {
    case ObjectRecord::Style::Clean:  return "clean";
    case ObjectRecord::Style::Marker: return "marker";
    case ObjectRecord::Style::Ink:    return "ink";
    }
    return "clean";
}

ObjectRecord::Style styleFromName(QStringView name)
{
    for (int i = 0; i <= int(StyleProperties::k_lastKind); ++i) {
        const auto s = static_cast<ObjectRecord::Style>(i);
        if (name == QLatin1String(styleName(s)))
            return s;
    }
    return ObjectRecord::Style::Clean;
}

QJsonObject ObjectRecord::toJson() const
{
    const ObjectRecord& a = *this;
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

ObjectRecord ObjectRecord::fromJson(const QJsonObject& j)
{
    ObjectRecord a;

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

QJsonObject ObjectRecord::mapToJson(const Map& m)
{
    QJsonObject j;
    for (auto it = m.begin(); it != m.end(); ++it)
        j.insert(it.key(), it.value().toJson());
    return j;
}

ObjectRecord::Map ObjectRecord::mapFromJson(const QJsonObject& j)
{
    Map m;
    for (auto it = j.begin(); it != j.end(); ++it)
        m.insert(it.key(), fromJson(it.value().toObject()));
    return m;
}

void topUpStyleSeed(ObjectRecord& a)
{
    // A bubble authored before styles existed carries seed 0, and so would every other one — style a
    // page of them and they would all wear the same wobble. Give it one the first time it is styled.
    if (a.style.kind != ObjectRecord::Style::Clean && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();
}
