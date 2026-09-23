#include "colouradjustment.hpp"

#include <QLocale>
#include <QObject>

namespace StripEdit {

namespace {

constexpr int k_valueDecimals = 2;   //!< Enough to tell 1.05 from 1.00, which is where a grade becomes visible.

[[nodiscard]] QString number(double v)
{
    return QLocale().toString(v, 'f', k_valueDecimals);
}

//! Neutral is whatever a default-constructed grade holds — the library's own definition, not a copy of its
//! numbers that could drift from it.
[[nodiscard]] const Platemaker::Models::ColourCorrection& neutral()
{
    static const Platemaker::Models::ColourCorrection n{};
    return n;
}

}  // namespace

QList<ColourAdjustment> allColourAdjustments()
{
    return {ColourAdjustment::Curves, ColourAdjustment::BrightnessContrast, ColourAdjustment::Saturation};
}

QString colourAdjustmentName(ColourAdjustment a)
{
    switch (a) {
    case ColourAdjustment::Curves:             return QObject::tr("Curves");
    case ColourAdjustment::BrightnessContrast: return QObject::tr("Brightness & contrast");
    case ColourAdjustment::Saturation:         return QObject::tr("Saturation");
    }
    return {};
}

bool isColourAdjustmentEditable(ColourAdjustment a)
{
    return a != ColourAdjustment::Curves;
}

bool isColourAdjustmentApplied(const Platemaker::Models::ColourCorrection& cc, ColourAdjustment a)
{
    const auto& n = neutral();
    switch (a) {
    case ColourAdjustment::Curves:             return Platemaker::Models::hasAnyCurve(cc.curves);
    case ColourAdjustment::BrightnessContrast: return cc.brightness != n.brightness || cc.contrast != n.contrast;
    case ColourAdjustment::Saturation:         return cc.saturation != n.saturation;
    }
    return false;
}

QString colourAdjustmentValues(const Platemaker::Models::ColourCorrection& cc, ColourAdjustment a)
{
    switch (a) {
    case ColourAdjustment::Curves:
        return {};
    case ColourAdjustment::BrightnessContrast:
        return QObject::tr("brightness %1, contrast %2").arg(number(cc.brightness), number(cc.contrast));
    case ColourAdjustment::Saturation:
        return number(cc.saturation);
    }
    return {};
}

Platemaker::Models::ColourCorrection withoutColourAdjustment(Platemaker::Models::ColourCorrection cc,
                                                             ColourAdjustment a)
{
    const auto& n = neutral();
    switch (a) {
    case ColourAdjustment::Curves:
        cc.curves = n.curves;
        break;
    case ColourAdjustment::BrightnessContrast:
        cc.brightness = n.brightness;
        cc.contrast   = n.contrast;
        break;
    case ColourAdjustment::Saturation:
        cc.saturation = n.saturation;
        break;
    }
    return cc;
}

}  // namespace StripEdit
