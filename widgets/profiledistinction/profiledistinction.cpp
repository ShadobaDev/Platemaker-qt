#include "profiledistinction.hpp"

#include <QCoreApplication>
#include <QPalette>
#include <QStringList>

using Platemaker::Models::CanvasProfile;
using Platemaker::Models::ProfileField;
using Platemaker::Models::RGBA;
using Platemaker::Models::TemplateFormat;

namespace {

QString tr(const char* text) { return QCoreApplication::translate("ProfileDistinction", text); }

QString hex(const RGBA& c)
{
    return QStringLiteral("%1%2%3%4").arg(c.r, 2, 16, QLatin1Char('0')).arg(c.g, 2, 16, QLatin1Char('0'))
                                     .arg(c.b, 2, 16, QLatin1Char('0')).arg(c.a, 2, 16, QLatin1Char('0'));
}

//! How one field reads in a list: what the artist set, not what it is called.
QString displayText(const CanvasProfile& cp, ProfileField field)
{
    switch (field) {
    case ProfileField::CanvasSize:
        return QStringLiteral("%1×%2").arg(cp.canvasSize.width).arg(cp.canvasSize.height);
    case ProfileField::Margins:
        return tr("margins %1, %2, %3, %4").arg(cp.margins.top).arg(cp.margins.right)
                                           .arg(cp.margins.bottom).arg(cp.margins.left);
    case ProfileField::Dpi:
        return tr("%1 dpi").arg(cp.dpi);
    case ProfileField::TemplateSettings:
        if (cp.templateSettings.format == TemplateFormat::Png)
            return tr("PNG template");
        return cp.templateSettings.emptyLayer ? tr("PSD template + layer") : tr("PSD template");
    case ProfileField::Colours:
        return tr("colours #%1 / #%2").arg(hex(cp.visualColour), hex(cp.backgroundColour));
    }
    return {};
}

//! The same field in a file name: letters, digits, '-' only.
QString fileText(const CanvasProfile& cp, ProfileField field)
{
    switch (field) {
    case ProfileField::CanvasSize:
        return QStringLiteral("%1x%2").arg(cp.canvasSize.width).arg(cp.canvasSize.height);
    case ProfileField::Margins:
        return QStringLiteral("m%1-%2-%3-%4").arg(cp.margins.top).arg(cp.margins.right)
                                             .arg(cp.margins.bottom).arg(cp.margins.left);
    case ProfileField::Dpi:
        return QStringLiteral("%1dpi").arg(cp.dpi).replace(QLatin1Char('.'), QLatin1Char('-'));
    case ProfileField::TemplateSettings:
        if (cp.templateSettings.format == TemplateFormat::Png)
            return QStringLiteral("png");
        return cp.templateSettings.emptyLayer ? QStringLiteral("psd-layer") : QStringLiteral("psd");
    case ProfileField::Colours:
        return QStringLiteral("c") + hex(cp.visualColour);
    }
    return {};
}

} // namespace

QList<Badge> profileDistinctionBadges(const std::vector<CanvasProfile>& palette, const CanvasProfile& cp,
                                      const QPalette& colours)
{
    const auto d    = Platemaker::Models::distinguishProfile(palette, cp);
    const QString name = QString::fromStdString(cp.name);
    QList<Badge> chips;
    if (d.duplicate)
        chips << toneBadge(BadgeTone::Warning, tr("Duplicate"),
                           tr("Identical in every setting to another profile named “%1”. One is enough — "
                              "the other can be deleted.").arg(name),
                           colours);
    if (!d.differs.empty()) {
        QStringList all;
        for (const ProfileField f : d.differs) all << displayText(cp, f);
        chips << toneBadge(BadgeTone::Neutral, all.first(),
                           tr("Another profile is named “%1” too. This one is set apart by: %2.")
                               .arg(name, all.join(QStringLiteral(", "))),
                           colours);
    }
    return chips;
}

QString profileDistinctionText(const std::vector<CanvasProfile>& palette, const CanvasProfile& cp)
{
    const auto d = Platemaker::Models::distinguishProfile(palette, cp);
    if (!d.differs.empty()) return displayText(cp, d.differs.front());
    return d.duplicate ? tr("duplicate") : QString{};
}

QString profileFileSuffix(const std::vector<CanvasProfile>& palette, const CanvasProfile& cp)
{
    const auto d = Platemaker::Models::distinguishProfile(palette, cp);
    return d.differs.empty() ? QString{} : QLatin1Char('_') + fileText(cp, d.differs.front());
}
