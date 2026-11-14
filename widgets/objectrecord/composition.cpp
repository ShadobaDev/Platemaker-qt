#include "composition.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QJsonArray>

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

QJsonObject tailToJson(const Tail& t)
{
    return {
        {QStringLiteral("tip"),   pointToJson(t.tip)},
        {QStringLiteral("width"), t.baseWidth},
        {QStringLiteral("bend"),  t.bend},
        {QStringLiteral("look"),  groupToJson(t.look)},
    };
}

Tail tailFromJson(const QJsonObject& o)
{
    Tail t;
    pointFromJson(o.value(QStringLiteral("tip")), t.tip);
    t.baseWidth = o.value(QStringLiteral("width")).toDouble(t.baseWidth);
    t.bend      = o.value(QStringLiteral("bend")).toDouble(t.bend);
    groupFromJson(t.look, o.value(QStringLiteral("look")).toObject());
    return t;
}

QJsonArray tailsToJson(const QList<Tail>& tails)
{
    QJsonArray arr;
    for (const Tail& t : tails)
        arr.append(tailToJson(t));
    return arr;
}

QList<Tail> tailsFromJson(const QJsonArray& arr)
{
    QList<Tail> tails;
    tails.reserve(arr.size());
    for (const QJsonValue& v : arr)
        tails.append(tailFromJson(v.toObject()));
    return tails;
}

//! A size as [width, height], and back; a value that is not one leaves @p v as it is.
QJsonArray sizeToJson(const QSize& s) { return {s.width(), s.height()}; }
void       sizeFromJson(const QJsonValue& j, QSize& v)
{
    const QJsonArray a = j.toArray();
    if (a.size() == 2)
        v = QSize(a[0].toInt(v.width()), a[1].toInt(v.height()));
}

//! A place: its position, and its rotation and scale only when they are not their defaults.
QJsonObject placeToJson(const Place& p)
{
    QJsonObject j{{QStringLiteral("pos"), pointToJson(p.pos)}};
    const Place defaults;
    if (!qFuzzyCompare(1.0 + p.rotation, 1.0 + defaults.rotation))
        j.insert(QStringLiteral("rotation"), p.rotation);
    if (!qFuzzyCompare(p.scale, defaults.scale))
        j.insert(QStringLiteral("scale"), p.scale);
    return j;
}

Place placeFromJson(const QJsonObject& j)
{
    Place p;
    pointFromJson(j.value(QStringLiteral("pos")), p.pos);
    p.rotation = j.value(QStringLiteral("rotation")).toDouble(p.rotation);
    p.scale    = j.value(QStringLiteral("scale")).toDouble(p.scale);
    return p;
}

} // namespace

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


const Body* seatOf(const Composition& c, const Words& w)
{
    if (!c.balloon || w.seat == 0)
        return nullptr;
    for (const Body& b : c.balloon->bodies)
        if (b.id == w.seat)
            return &b;
    return nullptr;
}

const Words* wordsOf(const Composition& c)
{
    return c.words.isEmpty() ? nullptr : &c.words.first();
}

QRectF frameOf(const Composition& c)
{
    QRectF r;
    const auto add = [&r](const Place& p, const QSize& s) {
        const QRectF one(p.pos, QSizeF(s));
        r = r.isNull() ? one : r.united(one);
    };
    if (c.balloon)
        for (const Body& b : c.balloon->bodies)
            add(b.place, b.size);
    if (c.picture)
        add(c.picture->place, c.picture->size);
    for (const Words& w : c.words)
        if (!seatOf(c, w))
            add(w.place, w.box);
    return r;
}

Composition compositionOf(const ObjectRecord& a)
{
    Composition c;
    c.seed   = a.styleSeed;
    c.preset = a.presetOrigin;

    int seat = 0;
    if (a.hasSilhouette()) {
        Balloon b;
        b.id       = c.nextId++;
        b.line     = a.style;
        b.fill     = a.skin;
        b.texture  = a.texture;
        b.effects  = a.effects;
        b.nextLook = a.tails.nextLook;
        Body body;
        body.id    = c.nextId++;
        body.size  = a.box;
        body.shape = a.shape;
        b.bodies.append(body);
        for (const Tail& t : a.tails.items)
            b.tails.append({c.nextId++, body.id, t});
        seat      = body.id;
        c.balloon = b;
    } else if (a.isArtwork()) {
        Picture p;
        p.id      = c.nextId++;
        p.file    = a.artwork;
        p.size    = a.box;
        c.picture = p;
    }

    Words w;
    w.id   = c.nextId++;
    w.seat = seat;
    if (seat == 0)
        w.box = a.box;
    w.text = a.text;
    c.words.append(w);
    return c;
}

ObjectRecord recordOf(const Composition& c)
{
    ObjectRecord a;
    a.styleSeed    = c.seed;
    a.presetOrigin = c.preset;
    a.shape.kind   = ObjectRecord::Shape::None;
    if (!c.words.isEmpty()) {
        a.text = c.words.first().text;
        a.box  = c.words.first().box;
    }

    if (c.balloon) {
        // A record is one body, the first: none has more before the chain.
        const Balloon& b = *c.balloon;
        a.style          = b.line;
        a.skin           = b.fill;
        a.texture        = b.texture;
        a.effects        = b.effects;
        a.tails.nextLook = b.nextLook;
        a.shape          = b.bodies.first().shape;
        a.box            = b.bodies.first().size;
        for (const BalloonTail& t : b.tails)
            a.tails.items.append(t.tail);
    } else if (c.picture) {
        a.artwork = c.picture->file;
        a.box     = c.picture->size;
    }
    return a;
}

QJsonObject toJson(const Composition& c)
{
    QJsonObject j{{QStringLiteral("seed"), double(c.seed)}, {QStringLiteral("nextId"), c.nextId}};
    if (c.balloon) {
        const Balloon& b = *c.balloon;
        QJsonArray     bodies;
        for (const Body& body : b.bodies)
            bodies.append(QJsonObject{
                {QStringLiteral("id"),    body.id},
                {QStringLiteral("place"), placeToJson(body.place)},
                {QStringLiteral("size"),  sizeToJson(body.size)},
                {QStringLiteral("shape"), groupToJson(body.shape)},
            });
        QJsonArray tails;
        for (const BalloonTail& t : b.tails) {
            QJsonObject o = tailToJson(t.tail);
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("root"), t.root);
            tails.append(o);
        }
        j.insert(QStringLiteral("balloon"), QJsonObject{
            {QStringLiteral("id"),       b.id},
            {QStringLiteral("line"),     groupToJson(b.line)},
            {QStringLiteral("fill"),     groupToJson(b.fill)},
            {QStringLiteral("texture"),  groupToJson(b.texture)},
            {QStringLiteral("effects"),  groupToJson(b.effects)},
            {QStringLiteral("nextTail"), groupToJson(b.nextLook)},
            {QStringLiteral("bodies"),   bodies},
            {QStringLiteral("tails"),    tails},
        });
    }
    if (c.picture)
        j.insert(QStringLiteral("picture"), QJsonObject{
            {QStringLiteral("id"),    c.picture->id},
            {QStringLiteral("file"),  c.picture->file},
            {QStringLiteral("place"), placeToJson(c.picture->place)},
            {QStringLiteral("size"),  sizeToJson(c.picture->size)},
        });
    QJsonArray words;
    for (const Words& w : c.words) {
        QJsonObject o{{QStringLiteral("id"), w.id}, {QStringLiteral("text"), groupToJson(w.text)}};
        if (w.seat != 0) {
            o.insert(QStringLiteral("seat"), w.seat);
        } else {   // their own box, read only then
            o.insert(QStringLiteral("place"), placeToJson(w.place));
            o.insert(QStringLiteral("box"), sizeToJson(w.box));
        }
        words.append(o);
    }
    j.insert(QStringLiteral("words"), words);
    if (!c.preset.isEmpty())
        j.insert(QStringLiteral("preset"), QJsonObject{{QStringLiteral("uid"), c.preset.uid},
                                                       {QStringLiteral("name"), c.preset.name}});
    return j;
}

Composition compositionFromJson(const QJsonObject& j)
{
    // Every field is read with the struct's own default as the fallback: a key left out is its default.
    Composition c;
    c.seed     = quint32(j.value(QStringLiteral("seed")).toDouble(0));
    int lastId = 0;
    const auto id = [&lastId](const QJsonObject& o) {
        const int n = o.value(QStringLiteral("id")).toInt();
        lastId      = qMax(lastId, n);
        return n;
    };

    if (const QJsonObject o = j.value(QStringLiteral("balloon")).toObject(); !o.isEmpty()) {
        Balloon b;
        b.id = id(o);
        groupFromJson(b.line, o.value(QStringLiteral("line")).toObject());
        groupFromJson(b.fill, o.value(QStringLiteral("fill")).toObject());
        groupFromJson(b.texture, o.value(QStringLiteral("texture")).toObject());
        groupFromJson(b.effects, o.value(QStringLiteral("effects")).toObject());
        groupFromJson(b.nextLook, o.value(QStringLiteral("nextTail")).toObject());
        for (const QJsonValue& v : o.value(QStringLiteral("bodies")).toArray()) {
            const QJsonObject bo = v.toObject();
            Body body;
            body.id    = id(bo);
            body.place = placeFromJson(bo.value(QStringLiteral("place")).toObject());
            sizeFromJson(bo.value(QStringLiteral("size")), body.size);
            groupFromJson(body.shape, bo.value(QStringLiteral("shape")).toObject());
            b.bodies.append(body);
        }
        for (const QJsonValue& v : o.value(QStringLiteral("tails")).toArray()) {
            const QJsonObject to = v.toObject();
            b.tails.append({id(to), to.value(QStringLiteral("root")).toInt(), tailFromJson(to)});
        }
        if (!b.bodies.isEmpty())   // a balloon is its bodies: one without any is not one
            c.balloon = b;
    }
    // One background at most: a picture beside a balloon is not read.
    if (const QJsonObject o = j.value(QStringLiteral("picture")).toObject(); !o.isEmpty() && !c.balloon) {
        Picture p;
        p.id    = id(o);
        p.file  = o.value(QStringLiteral("file")).toString();
        p.place = placeFromJson(o.value(QStringLiteral("place")).toObject());
        sizeFromJson(o.value(QStringLiteral("size")), p.size);
        c.picture = p;
    }
    for (const QJsonValue& v : j.value(QStringLiteral("words")).toArray()) {
        const QJsonObject o = v.toObject();
        Words w;
        w.id    = id(o);
        w.seat  = o.value(QStringLiteral("seat")).toInt();
        w.place = placeFromJson(o.value(QStringLiteral("place")).toObject());
        sizeFromJson(o.value(QStringLiteral("box")), w.box);
        groupFromJson(w.text, o.value(QStringLiteral("text")).toObject());
        c.words.append(w);
    }
    const QJsonObject preset = j.value(QStringLiteral("preset")).toObject();
    c.preset = {preset.value(QStringLiteral("uid")).toString(), preset.value(QStringLiteral("name")).toString()};
    // Never below an id in use, so an id is never given twice whatever the file says.
    c.nextId = qMax(j.value(QStringLiteral("nextId")).toInt(), lastId + 1);
    return c;
}
