#include "objectrecord.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QJsonArray>
#include <QObject>
#include <QRandomGenerator>

#include <type_traits>

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
QJsonValue toJsonValue(ObjectRecord::Pattern v) { return QLatin1String(patternName(v)); }
QJsonValue toJsonValue(ObjectRecord::Line v) { return QLatin1String(lineName(v)); }
QJsonValue toJsonValue(ObjectRecord::Texture v) { return QLatin1String(textureName(v)); }
QJsonValue toJsonValue(TailLook::Style v) { return QLatin1String(Geometry::tailStyleInfo(v).name); }
QJsonValue toJsonValue(TailLook::Tip v) { return QLatin1String(Geometry::tailTipInfo(v).name); }
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
void fromJsonValue(const QJsonValue& j, ObjectRecord::Pattern& v) { v = patternFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ObjectRecord::Line& v) { v = lineFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ObjectRecord::Texture& v) { v = textureFromName(j.toString()); }
// A tail's style and tip: an unknown or missing name leaves the value as it is.
void fromJsonValue(const QJsonValue& j, TailLook::Style& v)
{
    for (int i = 0; i <= int(TailLook::k_lastStyle); ++i)
        if (j.toString() == QLatin1String(Geometry::tailStyleInfo(TailLook::Style(i)).name))
            v = TailLook::Style(i);
}
void fromJsonValue(const QJsonValue& j, TailLook::Tip& v)
{
    for (int i = 0; i <= int(TailLook::k_lastTip); ++i)
        if (j.toString() == QLatin1String(Geometry::tailTipInfo(TailLook::Tip(i)).name))
            v = TailLook::Tip(i);
}
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

//! The sub-group a Nested field is in, for a field of a section (fields.hpp); void for any other field.
template <class M>
struct SectionOf
{
    using type = void;
};
template <class Group, class Sub, class T>
struct SectionOf<Fields::Nested<Group, Sub, T>>
{
    template <class S, class = void>
    struct Described : std::false_type {};
    template <class S>
    struct Described<S, std::void_t<decltype(S::section())>> : std::true_type {};
    //! A sub-group described by a section() (FlashParams…), not a Paint, whose fields stay the group's own.
    using type = std::conditional_t<Described<Sub>::value, Sub, void>;
};

/**
 * \brief Every field of \p g into \p j: a section's under the section's name, as an object of its own, leaving out
 *        those equal to the section's defaults (a reader takes the default for a key it does not find); every
 *        other field under its key, whatever its value.
 */
template <class Group>
QJsonObject groupToJson(const Group& g)
{
    QJsonObject j;
    Group::fields([&](const char* key, auto member) {
        using Sub = typename SectionOf<decltype(member)>::type;
        if constexpr (std::is_void_v<Sub>) {
            j.insert(QLatin1String(key), toJsonValue(Fields::at(g, member)));
        } else {
            static const Sub defaults{};
            const auto&      value = Fields::at(g, member);
            if (Fields::same(value, defaults.*member.inner))
                return;
            const QString name    = QLatin1String(Sub::section().name);
            QJsonObject   section = j.value(name).toObject();
            section.insert(QLatin1String(key), toJsonValue(value));
            j.insert(name, section);
        }
    });
    return j;
}

//! Every field of \p g out of \p j, groupToJson()'s way; a key \p j does not have leaves its field as it is.
template <class Group>
void groupFromJson(Group& g, const QJsonObject& j)
{
    Group::fields([&](const char* key, auto member) {
        using Sub = typename SectionOf<decltype(member)>::type;
        if constexpr (std::is_void_v<Sub>)
            fromJsonValue(j.value(QLatin1String(key)), Fields::at(g, member));
        else
            fromJsonValue(j.value(QLatin1String(Sub::section().name)).toObject().value(QLatin1String(key)),
                          Fields::at(g, member));
    });
}

//! A point as [x, y], and back; a value that is not one leaves @p v as it is.
QJsonArray pointToJson(const QPointF& p) { return {p.x(), p.y()}; }
void       pointFromJson(const QJsonValue& j, QPointF& v)
{
    const QJsonArray a = j.toArray();
    if (a.size() == 2)
        v = QPointF(a[0].toDouble(v.x()), a[1].toDouble(v.y()));
}

QJsonArray tailsToJson(const QList<Tail>& tails)
{
    QJsonArray arr;
    for (const Tail& t : tails)
        arr.append(QJsonObject{
            {QStringLiteral("tip"),   pointToJson(t.tip)},
            {QStringLiteral("width"), t.baseWidth},
            {QStringLiteral("bend"),  t.bend},
            {QStringLiteral("look"),  groupToJson(t.look)},
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
        pointFromJson(o.value(QStringLiteral("tip")), t.tip);
        t.baseWidth = o.value(QStringLiteral("width")).toDouble(t.baseWidth);
        t.bend      = o.value(QStringLiteral("bend")).toDouble(t.bend);
        groupFromJson(t.look, o.value(QStringLiteral("look")).toObject());
        tails.append(t);
    }
    return tails;
}

} // namespace

TailLook TailLook::fromFields(const QStringList& fields, qsizetype first)
{
    TailLook   look;
    const auto field = [&](qsizetype i) { return first + i < fields.size() ? fields[first + i] : QString(); };
    for (int i = 0; i <= int(k_lastStyle); ++i)
        if (field(0) == QLatin1String(Geometry::tailStyleInfo(Style(i)).name))
            look.style = Style(i);
    bool ok = false;
    if (const int n = field(1).toInt(&ok); ok)
        look.circles.count = n;
    if (const qreal v = field(2).toDouble(&ok); ok)
        look.circles.pitch = v;
    if (const qreal v = field(3).toDouble(&ok); ok)
        look.circles.offset = v;
    for (int i = 0; i <= int(k_lastTip); ++i)
        if (field(4) == QLatin1String(Geometry::tailTipInfo(Tip(i)).name))
            look.tip = Tip(i);
    if (const int n = field(5).toInt(&ok); ok)
        look.end.width = n;
    if (const int n = field(6).toInt(&ok); ok)
        look.breaks.count = n;
    if (const int n = field(7).toInt(&ok); ok)
        look.breaks.jog = n;
    if (const int n = field(8).toInt(&ok); ok)
        look.breaks.clockwise = n != 0;
    return look;
}

// ---------------------------------------------------------------------------
// ObjectRecord
// ---------------------------------------------------------------------------

bool ObjectRecord::operator==(const ObjectRecord& o) const
{
    // Group by group, so adding a property to a group cannot quietly fall out of equality: the
    // group's own operator== is the one place that has to know about it.
    return shape == o.shape && artwork == o.artwork && box == o.box && tails == o.tails
        && text == o.text && style == o.style && texture == o.texture && effects == o.effects && styleSeed == o.styleSeed
        && skin == o.skin && presetOrigin == o.presetOrigin;
}

ShapeProperties ShapeProperties::from(const ObjectRecord& a) { return a.shape; }
void            ShapeProperties::applyTo(ObjectRecord& a) const { a.shape = *this; }

StyleProperties StyleProperties::from(const ObjectRecord& a) { return a.style; }
void            StyleProperties::applyTo(ObjectRecord& a) const { a.style = *this; }

TextureProperties TextureProperties::from(const ObjectRecord& a) { return a.texture; }
void              TextureProperties::applyTo(ObjectRecord& a) const { a.texture = *this; }

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
    if (index < 0 || index >= a.tails.items.size())
        return;
    if (a.tails.items.at(index).look != tail.look)
        a.tails.nextLook = tail.look;   // a look picked: the next tail takes it
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

const char* patternName(ObjectRecord::Pattern f)
{
    return Geometry::patternInfo(f).name;
}

QString patternTitle(ObjectRecord::Pattern f)
{
    return QObject::tr(Geometry::patternInfo(f).title);
}

ObjectRecord::Pattern patternFromName(QStringView name)
{
    for (int i = 0; i <= int(Paint::k_lastKind); ++i) {
        const auto f = static_cast<ObjectRecord::Pattern>(i);
        if (name == QLatin1String(patternName(f)))
            return f;
    }
    return ObjectRecord::Pattern::Solid;
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
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto t = static_cast<ObjectRecord::Texture>(i);
        if (name == QLatin1String(textureName(t)))
            return t;
    }
    return ObjectRecord::Texture::None;
}

QJsonObject ObjectRecord::toJson() const
{
    const ObjectRecord& a = *this;
    // The record's own fields, which are not groups of scalars, by hand; then a group an object.
    QJsonObject j{
        {QStringLiteral("box"),      QJsonArray{a.box.width(), a.box.height()}},
        {QStringLiteral("seed"),     double(a.styleSeed)},
        {QStringLiteral("shape"),    groupToJson(a.shape)},
        {QStringLiteral("text"),     groupToJson(a.text)},
        {QStringLiteral("skin"),     groupToJson(a.skin)},
        {QStringLiteral("style"),    groupToJson(a.style)},
        {QStringLiteral("texture"),  groupToJson(a.texture)},
        {QStringLiteral("effects"),  groupToJson(a.effects)},
        {QStringLiteral("tails"),    tailsToJson(a.tails.items)},
        {QStringLiteral("nextTail"), groupToJson(a.tails.nextLook)},
    };
    if (!a.artwork.isEmpty())
        j.insert(QStringLiteral("artwork"), a.artwork);
    if (!a.presetOrigin.isEmpty())
        j.insert(QStringLiteral("preset"), QJsonObject{{QStringLiteral("uid"), a.presetOrigin.uid},
                                                       {QStringLiteral("name"), a.presetOrigin.name}});
    return j;
}

ObjectRecord ObjectRecord::fromJson(const QJsonObject& j)
{
    // Every field is read with the struct's own default as the fallback: a key left out is its default.
    ObjectRecord a;
    a.artwork = j.value(QStringLiteral("artwork")).toString();   // absent → an object we draw
    if (const QJsonArray box = j.value(QStringLiteral("box")).toArray(); box.size() == 2)
        a.box = QSize(box[0].toInt(a.box.width()), box[1].toInt(a.box.height()));
    a.styleSeed = quint32(j.value(QStringLiteral("seed")).toDouble(0));
    groupFromJson(a.shape, j.value(QStringLiteral("shape")).toObject());
    groupFromJson(a.text, j.value(QStringLiteral("text")).toObject());
    groupFromJson(a.skin, j.value(QStringLiteral("skin")).toObject());
    groupFromJson(a.style, j.value(QStringLiteral("style")).toObject());
    groupFromJson(a.texture, j.value(QStringLiteral("texture")).toObject());
    groupFromJson(a.effects, j.value(QStringLiteral("effects")).toObject());
    a.tails.items = tailsFromJson(j.value(QStringLiteral("tails")).toArray());
    groupFromJson(a.tails.nextLook, j.value(QStringLiteral("nextTail")).toObject());
    const QJsonObject preset = j.value(QStringLiteral("preset")).toObject();
    a.presetOrigin = {preset.value(QStringLiteral("uid")).toString(), preset.value(QStringLiteral("name")).toString()};
    return a;
}

bool usesSeed(const ObjectRecord& a)
{
    const bool irregularEdge = a.shape.edge != ObjectRecord::Edge::None && a.shape.teeth.irregularity > 0;
    return a.hasSilhouette()
        && (a.style.line != ObjectRecord::Line::Clean || a.texture.kind != ObjectRecord::Texture::None
            || irregularEdge);
}

void topUpStyleSeed(ObjectRecord& a)
{
    // A bubble authored before styles existed carries seed 0, and so would every other one — style a
    // page of them and they would all wear the same wobble. Give it one the first time it is styled.
    if (usesSeed(a) && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();
}
