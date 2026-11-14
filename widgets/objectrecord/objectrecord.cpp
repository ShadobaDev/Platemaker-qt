#include "objectrecord.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QObject>
#include <QRandomGenerator>

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
