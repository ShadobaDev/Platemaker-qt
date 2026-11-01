#include "objectrecord.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QJsonArray>
#include <QObject>
#include <QRandomGenerator>

namespace {

// ---------------------------------------------------------------------------
// A field's value in the JSON, and back — one overload per type a group holds (fields.hpp). Reading
// falls back to the value already there, which for a fresh record is the struct's own default.
// ---------------------------------------------------------------------------

QJsonValue toJsonValue(int v) { return v; }
QJsonValue toJsonValue(bool v) { return v; }
QJsonValue toJsonValue(qreal v) { return v; }
QJsonValue toJsonValue(const QString& v) { return v; }
QJsonValue toJsonValue(const QColor& v) { return v.name(QColor::HexArgb); }
QJsonValue toJsonValue(ObjectRecord::Shape v) { return QLatin1String(shapeName(v)); }
QJsonValue toJsonValue(ObjectRecord::Edge v) { return QLatin1String(edgeName(v)); }
QJsonValue toJsonValue(ObjectRecord::Fill v) { return QLatin1String(fillName(v)); }
QJsonValue toJsonValue(ObjectRecord::Line v) { return QLatin1String(lineName(v)); }
QJsonValue toJsonValue(ObjectRecord::Texture v) { return QLatin1String(textureName(v)); }
//! Nodes as [x, y], or [x, y, type, inX, inY, outX, outY] for a Curve's node with handles, at full
//! precision: an undo step restores exactly what was there.
QJsonValue toJsonValue(const QList<ShapeNode>& v)
{
    QJsonArray arr;
    for (const ShapeNode& n : v) {
        QJsonArray node{n.at.x(), n.at.y()};
        if (!n.plain())
            for (const QJsonValue& f : {QJsonValue(QLatin1String(ShapeNode::typeName(n.type))), QJsonValue(n.in.x()),
                                        QJsonValue(n.in.y()), QJsonValue(n.out.x()), QJsonValue(n.out.y())})
                node.append(f);
        arr.append(node);
    }
    return arr;
}

void fromJsonValue(const QJsonValue& j, int& v) { v = j.toInt(v); }
void fromJsonValue(const QJsonValue& j, bool& v) { v = j.toBool(v); }
void fromJsonValue(const QJsonValue& j, qreal& v) { v = j.toDouble(v); }
void fromJsonValue(const QJsonValue& j, QString& v) { v = j.toString(v); }
void fromJsonValue(const QJsonValue& j, QColor& v)
{
    const QColor c(j.toString());
    if (c.isValid())
        v = c;
}
// An unknown or missing name reads as the reader's own fallback (Box, Clean, None).
void fromJsonValue(const QJsonValue& j, ObjectRecord::Shape& v) { v = shapeFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ObjectRecord::Edge& v) { v = edgeFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ObjectRecord::Fill& v) { v = fillFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ObjectRecord::Line& v) { v = lineFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ObjectRecord::Texture& v) { v = textureFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, QList<ShapeNode>& v)
{
    if (!j.isArray())
        return;   // missing: the list already there stays
    v.clear();
    for (const QJsonValue& p : j.toArray()) {
        const QJsonArray f = p.toArray();
        if (f.size() == 2)
            v.append(ShapeNode(f[0].toDouble(), f[1].toDouble()));
        else if (f.size() == 7)
            v.append(ShapeNode({f[0].toDouble(), f[1].toDouble()}, {f[3].toDouble(), f[4].toDouble()},
                               {f[5].toDouble(), f[6].toDouble()}, ShapeNode::typeFromName(f[2].toString())));
    }
}

//! Every field of \p g into \p j, under its key.
template <class Group>
void writeJson(const Group& g, QJsonObject& j)
{
    Group::fields([&](const char* key, auto member) { j.insert(QLatin1String(key), toJsonValue(g.*member)); });
}

//! Every field of \p g out of \p j; a key \p j does not have leaves its field as it is.
template <class Group>
void readJson(Group& g, const QJsonObject& j)
{
    Group::fields([&](const char* key, auto member) { fromJsonValue(j.value(QLatin1String(key)), g.*member); });
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
        && text == o.text && style == o.style && effects == o.effects && styleSeed == o.styleSeed
        && skin == o.skin;
}

ShapeProperties ShapeProperties::from(const ObjectRecord& a) { return a.shape; }
void            ShapeProperties::applyTo(ObjectRecord& a) const { a.shape = *this; }

StyleProperties StyleProperties::from(const ObjectRecord& a) { return a.style; }
void            StyleProperties::applyTo(ObjectRecord& a) const { a.style = *this; }

EffectProperties EffectProperties::from(const ObjectRecord& a) { return a.effects; }
void             EffectProperties::applyTo(ObjectRecord& a) const { a.effects = *this; }

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
    return Geometry::shapeInfo(s).name;
}

QString shapeTitle(ObjectRecord::Shape s)
{
    return QObject::tr(Geometry::shapeInfo(s).title);
}

QString recordLabel(const ObjectRecord& a)
{
    const QString first = a.text.body.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (!first.isEmpty()) {
        return first.length() > k_labelChars ? first.left(k_labelChars - 1) + QStringLiteral("…")
                                             : first;
    }
    // No words: what it is instead — its template's title, or its kind's when it matches none, so none
    // is listed as "(overlay)".
    if (a.shape.kind == ObjectRecord::Shape::None)
        return QObject::tr("(text)");
    const Geometry::ShapeTemplate* t = Geometry::templateOf(a.shape);
    const QString title = t ? QObject::tr(t->title) : shapeTitle(a.shape.kind);
    return QStringLiteral("(%1)").arg(title.toLower());
}

const QList<ObjectRecord::Shape>& shapeOrder()
{
    using Shape = ObjectRecord::Shape;
    static const QList<Shape> order{
        Shape::Box, Shape::Ellipse, Shape::Cloud, Shape::Polygon, Shape::Curve,
    };   // Shape::None is not a silhouette — see the header
    return order;
}

ObjectRecord::Shape shapeFromName(QStringView name)
{
    // The picker's list and the one kind it leaves out — not a bound of our own. A bound written
    // here (it was `<= Banner`) is passed silently by the next shape appended, which then loads from
    // every file as a Box; a shape missing from the picker is caught by the unit tests.
    if (name == QLatin1String(shapeName(ObjectRecord::Shape::None)))
        return ObjectRecord::Shape::None;
    for (ObjectRecord::Shape s : shapeOrder())
        if (name == QLatin1String(shapeName(s)))
            return s;
    return ObjectRecord::Shape::Box;
}

const char* fillName(ObjectRecord::Fill f)
{
    return Geometry::fillInfo(f).name;
}

QString fillTitle(ObjectRecord::Fill f)
{
    return QObject::tr(Geometry::fillInfo(f).title);
}

ObjectRecord::Fill fillFromName(QStringView name)
{
    for (int i = 0; i <= int(SkinProperties::k_lastFill); ++i) {
        const auto f = static_cast<ObjectRecord::Fill>(i);
        if (name == QLatin1String(fillName(f)))
            return f;
    }
    return ObjectRecord::Fill::Solid;
}

const char* edgeName(ObjectRecord::Edge e)
{
    return Geometry::edgeInfo(e).name;
}

QString edgeTitle(ObjectRecord::Edge e)
{
    return QObject::tr(Geometry::edgeInfo(e).title);
}

ObjectRecord::Edge edgeFromName(QStringView name)
{
    for (int i = 0; i <= int(ShapeProperties::k_lastEdge); ++i) {
        const auto e = static_cast<ObjectRecord::Edge>(i);
        if (name == QLatin1String(edgeName(e)))
            return e;
    }
    return ObjectRecord::Edge::None;
}

const char* lineName(ObjectRecord::Line l)
{
    return Geometry::lineInfo(l).name;
}

QString lineTitle(ObjectRecord::Line l)
{
    return QObject::tr(Geometry::lineInfo(l).title);
}

ObjectRecord::Line lineFromName(QStringView name)
{
    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto l = static_cast<ObjectRecord::Line>(i);
        if (name == QLatin1String(lineName(l)))
            return l;
    }
    return ObjectRecord::Line::Clean;
}

const char* textureName(ObjectRecord::Texture t)
{
    return Geometry::textureInfo(t).name;
}

QString textureTitle(ObjectRecord::Texture t)
{
    return QObject::tr(Geometry::textureInfo(t).title);
}

ObjectRecord::Texture textureFromName(QStringView name)
{
    for (int i = 0; i <= int(StyleProperties::k_lastTexture); ++i) {
        const auto t = static_cast<ObjectRecord::Texture>(i);
        if (name == QLatin1String(textureName(t)))
            return t;
    }
    return ObjectRecord::Texture::None;
}

QJsonObject ObjectRecord::toJson() const
{
    const ObjectRecord& a = *this;
    // The record's own fields, which are not groups of scalars, by hand; then every group's list.
    QJsonObject j{
        {QStringLiteral("artwork"),   a.artwork},
        {QStringLiteral("w"),         a.box.width()},
        {QStringLiteral("h"),         a.box.height()},
        {QStringLiteral("tails"),     tailsToJson(a.tails.items)},
        {QStringLiteral("styleSeed"), double(a.styleSeed)},
    };
    writeJson(a.shape, j);
    writeJson(a.text, j);
    writeJson(a.skin, j);
    writeJson(a.style, j);
    writeJson(a.effects, j);
    return j;
}

ObjectRecord ObjectRecord::fromJson(const QJsonObject& j)
{
    // Every field is read defensively with the struct's own default as the fallback, so a snapshot
    // written by an older build loads as a usable bubble rather than a blank.
    ObjectRecord a;
    a.artwork     = j.value(QStringLiteral("artwork")).toString();   // absent → an object we draw
    a.box         = QSize(j.value(QStringLiteral("w")).toInt(a.box.width()),
                          j.value(QStringLiteral("h")).toInt(a.box.height()));
    a.tails.items = tailsFromJson(j.value(QStringLiteral("tails")).toArray());
    a.styleSeed   = quint32(j.value(QStringLiteral("styleSeed")).toDouble(0));
    readJson(a.shape, j);
    readJson(a.text, j);
    readJson(a.skin, j);
    readJson(a.style, j);
    readJson(a.effects, j);
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

bool usesSeed(const ObjectRecord& a)
{
    const bool irregularEdge = a.shape.edge != ObjectRecord::Edge::None && a.shape.edgeIrregularity > 0;
    return a.hasSilhouette()
        && (a.style.line != ObjectRecord::Line::Clean || a.style.texture != ObjectRecord::Texture::None
            || irregularEdge);
}

void topUpStyleSeed(ObjectRecord& a)
{
    // A bubble authored before styles existed carries seed 0, and so would every other one — style a
    // page of them and they would all wear the same wobble. Give it one the first time it is styled.
    if (usesSeed(a) && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();
}
