// The line styles: one table entry each, and the room each needs past the geometry.

#include "styles.hpp"
#include "geometry.hpp"

#include <QObject>

#include <array>

namespace Geometry {

namespace {

/**
 *  A dried felt-tip: the edge wanders in long slow waves, about 6 px off a straight line, and stays
 *  sharp.
 */
constexpr NoiseRecipe k_marker{0.028, 3, 6.0, 0.0, 0.0};
/**
 *  Ink bleeds in finer waves and less far than a marker wanders, then softens. The blur's reach is
 *  reserved whole, whatever the amount.
 */
constexpr NoiseRecipe k_ink{0.075, 2, 3.0, 0.6, 2.0};

using Kind = StyleProperties::Kind;

//! In the enum's order, so a kind is its own index; styleInfo() checks every entry names its kind.
const std::array<StyleInfo, int(StyleProperties::k_lastKind) + 1> k_styles{{
    {Kind::Clean,  "clean",  QT_TRANSLATE_NOOP("QObject", "Clean"),  nullptr},
    {Kind::Marker, "marker", QT_TRANSLATE_NOOP("QObject", "Marker"), &k_marker},
    {Kind::Ink,    "ink",    QT_TRANSLATE_NOOP("QObject", "Ink"),    &k_ink},
}};

} // namespace

const StyleInfo& styleInfo(StyleProperties::Kind kind)
{
    const int i = int(kind);
    // A kind past the table is a file from a newer build, or a corrupt value: draw it clean, the
    // fallback the readers already use, rather than reading past the end.
    if (i < 0 || i >= int(k_styles.size()))
        return k_styles[int(Kind::Clean)];
    Q_ASSERT(k_styles[i].kind == kind);   // the table is out of the enum's order
    return k_styles[i];
}

bool hasFilter(const ObjectRecord& a)
{
    return styleInfo(a.style.kind).noise != nullptr;
}

qreal styleMargin(const ObjectRecord& a)
{
    const NoiseRecipe* noise = styleInfo(a.style.kind).noise;
    if (!noise)
        return 0.0;
    return noise->displacement * qBound(0.0, a.style.amount, 2.0) + noise->blurReach;
}

}  // namespace Geometry
