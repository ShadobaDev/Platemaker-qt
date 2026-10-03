#include "recordstore.hpp"

#include <QDir>
#include <QFileInfo>

// ---------------------------------------------------------------------------
// RecordStore
// ---------------------------------------------------------------------------

QString RecordStore::overlaysDir(const QString& workspacePath)
{
    if (workspacePath.isEmpty())
        return {};
    return QFileInfo(workspacePath).absolutePath() + QStringLiteral("/overlays");
}

QString RecordStore::ensureOverlaysDir(const QString& workspacePath)
{
    const QString dir = overlaysDir(workspacePath);
    if (dir.isEmpty())
        return {};
    return QDir().mkpath(dir) ? dir : QString{};
}

ObjectRecord::Map RecordStore::records(const QString& projectUid) const
{
    return m_byProject.value(projectUid);
}

void RecordStore::setRecords(const QString& projectUid, ObjectRecord::Map map)
{
    if (map.isEmpty())
        m_byProject.remove(projectUid);
    else
        m_byProject.insert(projectUid, std::move(map));
}
