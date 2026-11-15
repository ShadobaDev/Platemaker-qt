#include "compositionstore.hpp"

#include <QDir>
#include <QFileInfo>

// ---------------------------------------------------------------------------
// CompositionStore
// ---------------------------------------------------------------------------

QString CompositionStore::overlaysDir(const QString& workspacePath)
{
    if (workspacePath.isEmpty())
        return {};
    return QFileInfo(workspacePath).absolutePath() + QStringLiteral("/overlays");
}

QString CompositionStore::ensureOverlaysDir(const QString& workspacePath)
{
    const QString dir = overlaysDir(workspacePath);
    if (dir.isEmpty())
        return {};
    return QDir().mkpath(dir) ? dir : QString{};
}

Composition::Map CompositionStore::compositions(const QString& projectUid) const
{
    return m_byProject.value(projectUid);
}

void CompositionStore::setCompositions(const QString& projectUid, Composition::Map map)
{
    if (map.isEmpty())
        m_byProject.remove(projectUid);
    else
        m_byProject.insert(projectUid, std::move(map));
}
