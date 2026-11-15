#include "names.hpp"
#include "shapes.hpp"
#include "styles.hpp"

#include <QObject>

const char* shapeName(ShapeProperties::Kind s)
{
    return Geometry::shapeInfo(s).name;
}

QString shapeTitle(ShapeProperties::Kind s)
{
    return QObject::tr(Geometry::shapeInfo(s).title);
}

const QList<ShapeProperties::Kind>& shapeOrder()
{
    using Shape = ShapeProperties::Kind;
    static const QList<Shape> order{
        Shape::Box, Shape::Ellipse, Shape::Cloud, Shape::Polygon, Shape::Curve,
    };   // Shape::None is not a silhouette — see the header
    return order;
}

ShapeProperties::Kind shapeFromName(QStringView name)
{
    // The picker's list and the one kind it leaves out — not a bound of our own. A bound written
    // here (it was `<= Banner`) is passed silently by the next shape appended, which then loads from
    // every file as a Box; a shape missing from the picker is caught by the unit tests.
    if (name == QLatin1String(shapeName(ShapeProperties::Kind::None)))
        return ShapeProperties::Kind::None;
    for (ShapeProperties::Kind s : shapeOrder())
        if (name == QLatin1String(shapeName(s)))
            return s;
    return ShapeProperties::Kind::Box;
}

const char* patternName(Paint::Kind f)
{
    return Geometry::patternInfo(f).name;
}

QString patternTitle(Paint::Kind f)
{
    return QObject::tr(Geometry::patternInfo(f).title);
}

Paint::Kind patternFromName(QStringView name)
{
    for (int i = 0; i <= int(Paint::k_lastKind); ++i) {
        const auto f = static_cast<Paint::Kind>(i);
        if (name == QLatin1String(patternName(f)))
            return f;
    }
    return Paint::Kind::Solid;
}

const char* edgeName(ShapeProperties::Edge e)
{
    return Geometry::edgeInfo(e).name;
}

QString edgeTitle(ShapeProperties::Edge e)
{
    return QObject::tr(Geometry::edgeInfo(e).title);
}

ShapeProperties::Edge edgeFromName(QStringView name)
{
    for (int i = 0; i <= int(ShapeProperties::k_lastEdge); ++i) {
        const auto e = static_cast<ShapeProperties::Edge>(i);
        if (name == QLatin1String(edgeName(e)))
            return e;
    }
    return ShapeProperties::Edge::None;
}

const char* lineName(StyleProperties::Line l)
{
    return Geometry::lineInfo(l).name;
}

QString lineTitle(StyleProperties::Line l)
{
    return QObject::tr(Geometry::lineInfo(l).title);
}

StyleProperties::Line lineFromName(QStringView name)
{
    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto l = static_cast<StyleProperties::Line>(i);
        if (name == QLatin1String(lineName(l)))
            return l;
    }
    return StyleProperties::Line::Clean;
}

const char* textureName(TextureProperties::Kind t)
{
    return Geometry::textureInfo(t).name;
}

QString textureTitle(TextureProperties::Kind t)
{
    return QObject::tr(Geometry::textureInfo(t).title);
}

TextureProperties::Kind textureFromName(QStringView name)
{
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto t = static_cast<TextureProperties::Kind>(i);
        if (name == QLatin1String(textureName(t)))
            return t;
    }
    return TextureProperties::Kind::None;
}
