#include "composition.hpp"
#include "names.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QJsonArray>
#include <QObject>
#include <QRandomGenerator>

#include <type_traits>

namespace {

// ---------------------------------------------------------------------------
// A field's value in the JSON, and back — one overload per type a group holds (fields.hpp). Reading
// falls back to the value already there, which for a fresh element is the struct's own default.
// ---------------------------------------------------------------------------

QJsonValue toJsonValue(int v) { return v; }
QJsonValue toJsonValue(bool v) { return v; }
QJsonValue toJsonValue(qreal v) { return v; }
QJsonValue toJsonValue(const QString& v) { return v; }
QJsonValue toJsonValue(const QColor& v) { return v.name(QColor::HexArgb); }
QJsonValue toJsonValue(ShapeProperties::Kind v) { return QLatin1String(shapeName(v)); }
QJsonValue toJsonValue(ShapeProperties::Edge v) { return QLatin1String(edgeName(v)); }
QJsonValue toJsonValue(Paint::Kind v) { return QLatin1String(patternName(v)); }
QJsonValue toJsonValue(StyleProperties::Line v) { return QLatin1String(lineName(v)); }
QJsonValue toJsonValue(TextureProperties::Kind v) { return QLatin1String(textureName(v)); }
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
void fromJsonValue(const QJsonValue& j, ShapeProperties::Kind& v) { v = shapeFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, ShapeProperties::Edge& v) { v = edgeFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, Paint::Kind& v) { v = patternFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, StyleProperties::Line& v) { v = lineFromName(j.toString()); }
void fromJsonValue(const QJsonValue& j, TextureProperties::Kind& v) { v = textureFromName(j.toString()); }
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

Words* wordsOf(Composition& c)
{
    return c.words.isEmpty() ? nullptr : &c.words.first();
}

const Body* bodyOf(const Composition& c)
{
    return c.balloon && !c.balloon->bodies.isEmpty() ? &c.balloon->bodies.first() : nullptr;
}

Body* bodyOf(Composition& c)
{
    return c.balloon && !c.balloon->bodies.isEmpty() ? &c.balloon->bodies.first() : nullptr;
}

QSize sizeOf(const Composition& c)
{
    if (const Body* b = bodyOf(c))
        return b->size;
    if (c.picture)
        return c.picture->size;
    const Words* w = wordsOf(c);
    return w ? w->box : QSize();
}

void resize(Composition& c, const QSize& size)
{
    if (Body* b = bodyOf(c)) {
        b->size = size;
        return;
    }
    if (c.picture)
        c.picture->size = size;
    for (Words& w : c.words)
        if (!seatOf(c, w))
            w.box = size;   // a picture's words are over the whole of it
}

Composition textObject(const QSize& size)
{
    Composition c;
    Words       w;
    w.id  = c.nextId++;
    w.box = size;
    c.words.append(w);
    return c;
}

Composition withBalloon(Composition c, const BalloonLook& look)
{
    if (c.balloon || c.picture)
        return c;
    Balloon b;
    b.id    = c.nextId++;
    b.style = look.style;
    Body body;
    body.id    = c.nextId++;
    body.shape = look.shape;
    if (Words* w = wordsOf(c)) {
        body.place = w->place;
        body.size  = w->box;
        w->seat    = body.id;
        w->place   = {};   // read only with no seat
        w->box     = {};
    } else {
        body.size = k_defaultObjectSize;
    }
    b.bodies.append(body);
    c.balloon = b;
    return c;
}

Composition withoutBalloon(Composition c)
{
    if (!c.balloon)
        return c;
    for (Words& w : c.words)
        if (const Body* seat = seatOf(c, w)) {
            w.place = seat->place;
            w.box   = seat->size;
            w.seat  = 0;
        }
    c.balloon.reset();
    return c;
}

bool usesSeed(const Composition& c)
{
    const Body* body = bodyOf(c);
    if (!body)
        return false;
    const BalloonStyle& s = c.balloon->style;
    const bool irregularEdge = body->shape.edge != ShapeProperties::Edge::None && body->shape.teeth.irregularity > 0;
    return s.line.line != StyleProperties::Line::Clean || s.texture.kind != TextureProperties::Kind::None
        || irregularEdge;
}

void topUpStyleSeed(Composition& c)
{
    // An object authored before styles existed carries seed 0, and so would every other one — style a page of
    // them and they would all wear the same wobble. Give it one the first time it is styled.
    if (usesSeed(c) && c.seed == 0)
        c.seed = QRandomGenerator::global()->generate();
}

QString objectLabel(const Composition& c)
{
    const Words*  w     = wordsOf(c);
    const QString first = w ? w->text.body.section(QLatin1Char('\n'), 0, 0).trimmed() : QString();
    if (!first.isEmpty())
        return first.length() > k_labelChars ? first.left(k_labelChars - 1) + QStringLiteral("…") : first;
    // No words: what it is instead — its template's title, or its kind's when it matches none, so none is
    // listed as "(overlay)".
    const Body* body = bodyOf(c);
    if (!body)
        return QObject::tr("(text)");
    const Geometry::ShapeTemplate* t     = Geometry::templateOf(body->shape);
    const QString                  title = t ? QObject::tr(t->title) : shapeTitle(body->shape.kind);
    return QStringLiteral("(%1)").arg(title.toLower());
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

QJsonObject toJson(const BalloonStyle& s)
{
    return {
        {QStringLiteral("line"),     groupToJson(s.line)},
        {QStringLiteral("fill"),     groupToJson(s.fill)},
        {QStringLiteral("texture"),  groupToJson(s.texture)},
        {QStringLiteral("effects"),  groupToJson(s.effects)},
        {QStringLiteral("nextTail"), groupToJson(s.nextLook)},
    };
}

BalloonStyle balloonStyleFromJson(const QJsonObject& j)
{
    BalloonStyle s;
    groupFromJson(s.line, j.value(QStringLiteral("line")).toObject());
    groupFromJson(s.fill, j.value(QStringLiteral("fill")).toObject());
    groupFromJson(s.texture, j.value(QStringLiteral("texture")).toObject());
    groupFromJson(s.effects, j.value(QStringLiteral("effects")).toObject());
    groupFromJson(s.nextLook, j.value(QStringLiteral("nextTail")).toObject());
    return s;
}

QJsonObject toJson(const ShapeProperties& s)
{
    return groupToJson(s);
}

ShapeProperties shapeFromJson(const QJsonObject& j)
{
    ShapeProperties s;
    groupFromJson(s, j);
    return s;
}

QJsonObject toJson(const TextProperties& t)
{
    return groupToJson(t);
}

TextProperties textFromJson(const QJsonObject& j)
{
    TextProperties t;
    groupFromJson(t, j);
    return t;
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
                {QStringLiteral("shape"), toJson(body.shape)},
            });
        QJsonArray tails;
        for (const BalloonTail& t : b.tails) {
            QJsonObject o = tailToJson(t.tail);
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("root"), t.root);
            tails.append(o);
        }
        j.insert(QStringLiteral("balloon"), QJsonObject{
            {QStringLiteral("id"),     b.id},
            {QStringLiteral("style"),  toJson(b.style)},
            {QStringLiteral("bodies"), bodies},
            {QStringLiteral("tails"),  tails},
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
        QJsonObject o{{QStringLiteral("id"), w.id}, {QStringLiteral("text"), toJson(w.text)}};
        if (w.seat != 0) {
            o.insert(QStringLiteral("seat"), w.seat);
            if (w.anchor != Words().anchor)
                o.insert(QStringLiteral("anchor"), pointToJson(w.anchor));
            QJsonObject frame;
            for (const auto& [name, edge] : {std::pair{"left", w.frame.left}, std::pair{"top", w.frame.top},
                                             std::pair{"right", w.frame.right}, std::pair{"bottom", w.frame.bottom}})
                if (edge)
                    frame.insert(QLatin1String(name), *edge);
            if (!frame.isEmpty())
                o.insert(QStringLiteral("frame"), frame);
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
        b.style = balloonStyleFromJson(o.value(QStringLiteral("style")).toObject());
        for (const QJsonValue& v : o.value(QStringLiteral("bodies")).toArray()) {
            const QJsonObject bo = v.toObject();
            Body body;
            body.id    = id(bo);
            body.place = placeFromJson(bo.value(QStringLiteral("place")).toObject());
            sizeFromJson(bo.value(QStringLiteral("size")), body.size);
            body.shape = shapeFromJson(bo.value(QStringLiteral("shape")).toObject());
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
        w.text  = textFromJson(o.value(QStringLiteral("text")).toObject());
        pointFromJson(o.value(QStringLiteral("anchor")), w.anchor);
        const QJsonObject frame = o.value(QStringLiteral("frame")).toObject();
        for (const auto& [name, edge] : {std::pair{"left", &w.frame.left}, std::pair{"top", &w.frame.top},
                                         std::pair{"right", &w.frame.right}, std::pair{"bottom", &w.frame.bottom}})
            if (const QJsonValue v = frame.value(QLatin1String(name)); v.isDouble())
                *edge = v.toDouble();
        c.words.append(w);
    }
    const QJsonObject preset = j.value(QStringLiteral("preset")).toObject();
    c.preset = {preset.value(QStringLiteral("uid")).toString(), preset.value(QStringLiteral("name")).toString()};
    // Never below an id in use, so an id is never given twice whatever the file says.
    c.nextId = qMax(j.value(QStringLiteral("nextId")).toInt(), lastId + 1);
    return c;
}

// ---------------------------------------------------------------------------
// The property groups on a composition: the balloon's style, the first body, the first words
// ---------------------------------------------------------------------------

namespace {

//! What @p c's balloon has of a style group, or the group's default with none.
template <class Group>
Group styleGroup(const Composition& c, Group BalloonStyle::* member)
{
    return c.balloon ? c.balloon->style.*member : Group{};
}

template <class Group>
void setStyleGroup(Composition& c, Group BalloonStyle::* member, const Group& value)
{
    if (c.balloon)
        c.balloon->style.*member = value;
}

} // namespace

ShapeProperties ShapeProperties::from(const Composition& c)
{
    if (c.balloon && !c.balloon->bodies.isEmpty())
        return c.balloon->bodies.first().shape;
    ShapeProperties none;
    none.kind = Kind::None;
    return none;
}

void ShapeProperties::applyTo(Composition& c) const
{
    if (c.balloon && !c.balloon->bodies.isEmpty())
        c.balloon->bodies.first().shape = *this;
}

SkinProperties SkinProperties::from(const Composition& c) { return styleGroup(c, &BalloonStyle::fill); }
void           SkinProperties::applyTo(Composition& c) const { setStyleGroup(c, &BalloonStyle::fill, *this); }

StyleProperties StyleProperties::from(const Composition& c) { return styleGroup(c, &BalloonStyle::line); }
void            StyleProperties::applyTo(Composition& c) const { setStyleGroup(c, &BalloonStyle::line, *this); }

TextureProperties TextureProperties::from(const Composition& c) { return styleGroup(c, &BalloonStyle::texture); }
void              TextureProperties::applyTo(Composition& c) const { setStyleGroup(c, &BalloonStyle::texture, *this); }

EffectProperties EffectProperties::from(const Composition& c) { return styleGroup(c, &BalloonStyle::effects); }
void             EffectProperties::applyTo(Composition& c) const { setStyleGroup(c, &BalloonStyle::effects, *this); }

TextProperties TextProperties::from(const Composition& c)
{
    return c.words.isEmpty() ? TextProperties{} : c.words.first().text;
}

void TextProperties::applyTo(Composition& c) const
{
    if (!c.words.isEmpty())
        c.words.first().text = *this;
}

TailsProperties TailsProperties::from(const Composition& c)
{
    TailsProperties p;
    if (!c.balloon)
        return p;
    p.nextLook = c.balloon->style.nextLook;
    for (const BalloonTail& t : c.balloon->tails)
        p.items.append(t.tail);
    return p;
}

void TailsProperties::applyTo(Composition& c) const
{
    if (!c.balloon || c.balloon->bodies.isEmpty())
        return;
    Balloon& b      = *c.balloon;
    b.style.nextLook = nextLook;
    // A tail is kept by its place in the list, its id and its root with it; one past the end is new, from the
    // first body; the ones the list no longer has go.
    QList<BalloonTail> tails;
    for (qsizetype i = 0; i < items.size(); ++i)
        tails.append(i < b.tails.size() ? BalloonTail{b.tails.at(i).id, b.tails.at(i).root, items.at(i)}
                                        : BalloonTail{c.nextId++, b.bodies.first().id, items.at(i)});
    b.tails = tails;
}

TailProperties TailProperties::from(const Composition& c, int index)
{
    TailProperties p;
    p.index = index;
    if (c.balloon && index >= 0 && index < c.balloon->tails.size())
        p.tail = c.balloon->tails.at(index).tail;
    return p;
}

void TailProperties::applyTo(Composition& c) const
{
    if (!c.balloon || index < 0 || index >= c.balloon->tails.size())
        return;
    Tail& t = c.balloon->tails[index].tail;
    if (t.look != tail.look)
        c.balloon->style.nextLook = tail.look;   // a look picked: the next tail takes it
    t = tail;
}
