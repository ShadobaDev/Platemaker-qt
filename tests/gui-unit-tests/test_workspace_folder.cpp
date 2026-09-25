// One workspace per folder is only as good as the question "which files are workspaces". A profile bundle
// and the retired authoring sidecar also end in ".json" beside a workspace, and a workspace in a subfolder
// owns that subfolder — none of them may count, or the rule would refuse folders that are fine.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "workspacefolder.hpp"

namespace {

void touch(const QString& path)
{
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
}

} // namespace

TEST(WorkspaceFolder, CountsWorkspaceFilesAndNothingElse)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir d(tmp.path());

    touch(d.filePath(QStringLiteral("Chapter_002.platemaker.json")));
    touch(d.filePath(QStringLiteral("Chapter_003.platemaker.json")));
    touch(d.filePath(QStringLiteral("profiles.platemaker.profiles.json")));    // a profile bundle
    touch(d.filePath(QStringLiteral("Chapter_002.platemaker.overlays.json"))); // the retired sidecar
    touch(d.filePath(QStringLiteral("notes.json")));
    ASSERT_TRUE(d.mkpath(QStringLiteral("Chapter_004")));
    touch(d.filePath(QStringLiteral("Chapter_004/Chapter_004.platemaker.json"))); // owns its subfolder

    const QStringList found = workspacesInFolder(tmp.path());
    ASSERT_EQ(found.size(), 2);
    EXPECT_EQ(QFileInfo(found.at(0)).fileName(), QStringLiteral("Chapter_002.platemaker.json"));
    EXPECT_EQ(QFileInfo(found.at(1)).fileName(), QStringLiteral("Chapter_003.platemaker.json"));
    EXPECT_TRUE(QFileInfo(found.at(0)).isAbsolute());
}

TEST(WorkspaceFolder, AWorkspaceIsNotItsOwnNeighbour)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir    d(tmp.path());
    const QString mine = d.filePath(QStringLiteral("Chapter_002.platemaker.json"));
    touch(mine);

    // Alone in its folder: saving over itself, or opening it, is fine.
    EXPECT_TRUE(otherWorkspacesBeside(mine).isEmpty());

    // A second one arrives — by hand, or as a synced drive's conflict copy.
    touch(d.filePath(QStringLiteral("Chapter_002 (1).platemaker.json")));
    const QStringList others = otherWorkspacesBeside(mine);
    ASSERT_EQ(others.size(), 1);
    EXPECT_EQ(QFileInfo(others.first()).fileName(), QStringLiteral("Chapter_002 (1).platemaker.json"));

    // A file not written yet (New, Save As under a new name) has both existing ones as neighbours.
    EXPECT_EQ(otherWorkspacesBeside(d.filePath(QStringLiteral("Chapter_005.platemaker.json"))).size(), 2);
}

TEST(WorkspaceFolder, AMissingFolderHoldsNoWorkspaces)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_TRUE(workspacesInFolder(QDir(tmp.path()).filePath(QStringLiteral("absent"))).isEmpty());
}
