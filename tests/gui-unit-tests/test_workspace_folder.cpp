// One workspace per folder is only as good as the question "which files are workspaces". A profile bundle
// and the retired authoring sidecar also end in ".json" beside a workspace, and a workspace in a subfolder
// owns that subfolder — none of them may count, or the rule would refuse folders that are fine.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "fontfiles.hpp"
#include "workspacefolder.hpp"

#include <algorithm>

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

// ---------------------------------------------------------------------------
// Save As carries what the workspace made (collectOverlayFiles)
// ---------------------------------------------------------------------------

namespace {

void write(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray read(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}

Platemaker::Models::StripOverlay overlayAt(const QString& uid, const QString& path)
{
    Platemaker::Models::StripOverlay o;
    o.uid       = uid.toStdString();
    o.assetPath = path.toStdString();
    o.sha256    = "unchanged";
    return o;
}

//! An old workspace folder and the new one Save As is moving to, each with an overlays/ folder.
struct TwoFolders {
    QTemporaryDir old;
    QTemporaryDir fresh;
    [[nodiscard]] QString from(const QString& name) const { return QDir(old.path()).filePath("overlays/" + name); }
    [[nodiscard]] QString to(const QString& name) const { return QDir(fresh.path()).filePath("overlays/" + name); }
    [[nodiscard]] QString toDir() const
    {
        const QString d = QDir(fresh.path()).filePath(QStringLiteral("overlays"));
        QDir().mkpath(d);
        return d;
    }
};

} // namespace

TEST(CollectOverlayFiles, CopiesWhatIsOutsideAndLeavesWhatIsHome)
{
    TwoFolders f;
    write(f.from("ovl-a.svg"), "balloon");
    write(f.to("ovl-b.svg"), "already here");

    std::vector overlays{overlayAt("1", f.from("ovl-a.svg")), overlayAt("2", f.to("ovl-b.svg"))};
    ASSERT_TRUE(collectOverlayFiles(overlays, {}, f.toDir()));

    EXPECT_TRUE(QFileInfo(QString::fromStdString(overlays[0].assetPath)) == QFileInfo(f.to("ovl-a.svg")));
    EXPECT_EQ(read(f.to("ovl-a.svg")), QByteArray("balloon"));
    EXPECT_EQ(overlays[0].sha256, "unchanged");   // same bytes, same hash
    EXPECT_TRUE(QFileInfo(QString::fromStdString(overlays[1].assetPath)) == QFileInfo(f.to("ovl-b.svg")));
    EXPECT_TRUE(QFileInfo::exists(f.from("ovl-a.svg")));   // the old folder keeps its own

    // Running it again finds everything home and changes nothing.
    const auto before = overlays;
    ASSERT_TRUE(collectOverlayFiles(overlays, {}, f.toDir()));
    EXPECT_EQ(overlays[0].assetPath, before[0].assetPath);
    EXPECT_EQ(overlays[1].assetPath, before[1].assetPath);
}

TEST(CollectOverlayFiles, ALetteredPictureBringsThePictureItsRecordNames)
{
    // The overlay points at the wrapper; the picture is named only by the record. Following assetPath
    // alone would leave the picture behind and the object with nothing to re-letter.
    TwoFolders f;
    write(f.from("ovl-wrapper.svg"), "wrapper");
    write(f.from("art-picture.png"), "picture");

    ObjectRecord record;
    record.artwork = QStringLiteral("art-picture.png");
    const ObjectRecord::Map records{{QStringLiteral("1"), record}};

    std::vector overlays{overlayAt("1", f.from("ovl-wrapper.svg"))};
    ASSERT_TRUE(collectOverlayFiles(overlays, records, f.toDir()));
    EXPECT_EQ(read(f.to("ovl-wrapper.svg")), QByteArray("wrapper"));
    EXPECT_EQ(read(f.to("art-picture.png")), QByteArray("picture"));
}

TEST(CollectOverlayFiles, ADifferentFileWithTheSameNameIsNeverOverwritten)
{
    TwoFolders f;
    write(f.from("ovl-a.svg"), "ours");
    write(f.to("ovl-a.svg"), "someone else's");

    std::vector overlays{overlayAt("1", f.from("ovl-a.svg"))};
    ASSERT_TRUE(collectOverlayFiles(overlays, {}, f.toDir()));

    EXPECT_EQ(read(f.to("ovl-a.svg")), QByteArray("someone else's"));
    const QString landed = QString::fromStdString(overlays[0].assetPath);
    EXPECT_TRUE(QFileInfo(landed).fileName().startsWith(QStringLiteral("ovl-")));
    EXPECT_NE(QFileInfo(landed).fileName(), QStringLiteral("ovl-a.svg"));
    EXPECT_EQ(read(landed), QByteArray("ours"));
}

TEST(CollectOverlayFiles, APictureThatCannotKeepItsNameFailsAndChangesNothing)
{
    // A picture's name is what its record says; renaming it would orphan the record. So a clash fails the
    // whole collect — and all or nothing means the other overlay is not repointed either.
    TwoFolders f;
    write(f.from("ovl-a.svg"), "balloon");
    write(f.from("art-p.png"), "ours");
    write(f.to("art-p.png"), "someone else's");

    ObjectRecord picture;
    picture.artwork = QStringLiteral("art-p.png");
    const ObjectRecord::Map records{{QStringLiteral("2"), picture}};

    std::vector overlays{overlayAt("1", f.from("ovl-a.svg")), overlayAt("2", f.from("art-p.png"))};
    const auto before = overlays;
    QString failed;
    EXPECT_FALSE(collectOverlayFiles(overlays, records, f.toDir(), &failed));
    EXPECT_TRUE(QFileInfo(failed) == QFileInfo(f.from("art-p.png")));
    EXPECT_EQ(overlays[0].assetPath, before[0].assetPath);
    EXPECT_EQ(overlays[1].assetPath, before[1].assetPath);
}

TEST(CollectOverlayFiles, AMissingBalloonIsPointedHomeToBeRebuiltThere)
{
    // A balloon is drawn from its record, so a missing file is rewritten — here, never in the old folder.
    // A missing picture has nothing to be rebuilt from, and stays where it was.
    TwoFolders f;
    ObjectRecord picture;
    picture.artwork = QStringLiteral("art-gone.png");
    const ObjectRecord::Map records{{QStringLiteral("1"), ObjectRecord{}}, {QStringLiteral("2"), picture}};

    std::vector overlays{overlayAt("1", f.from("ovl-gone.svg")), overlayAt("2", f.from("art-gone.png"))};
    ASSERT_TRUE(collectOverlayFiles(overlays, records, f.toDir()));
    EXPECT_TRUE(QFileInfo(QString::fromStdString(overlays[0].assetPath)).absolutePath()
                == QFileInfo(f.toDir()).absoluteFilePath());
    EXPECT_TRUE(QFileInfo(QString::fromStdString(overlays[1].assetPath)) == QFileInfo(f.from("art-gone.png")));
}

// ---------------------------------------------------------------------------
// The sweep at open (unusedWorkspaceFiles): what it may take, and what it must leave
// ---------------------------------------------------------------------------

TEST(UnusedWorkspaceFiles, OnlyOurOwnUnreferencedFilesAreCandidates)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir d(tmp.path());
    const auto at = [&d](const char* rel) { return d.filePath(QString::fromLatin1(rel)); };

    write(at("Chapter.platemaker.json"), "{}");
    write(at("overlays/ovl-used.svg"), "used");
    write(at("overlays/ovl-orphan.svg"), "orphan");
    write(at("overlays/art-picture.png"), "picture behind a lettered picture");
    write(at("overlays/art-orphan.png"), "orphan picture");
    write(at("overlays/my-notes.txt"), "the user's");            // not our name
    write(at("templates/3p-m.png"), "used template");
    write(at("templates/old.png"), "deleted template");
    write(at("fonts/Comic.ttf"), "a font");                       // never swept
    write(at(".platemaker.lock"), "{}");

    const QStringList unused = unusedWorkspaceFiles(
        tmp.path(), {at("overlays/ovl-used.svg"), at("overlays/art-picture.png"), at("templates/3p-m.png"),
                     at("overlays/does-not-exist.svg")});

    QStringList names;
    for (const QString& p : unused)
        names << QDir(tmp.path()).relativeFilePath(p);
    names.sort();
    EXPECT_EQ(names, (QStringList{QStringLiteral("overlays/art-orphan.png"),
                                  QStringLiteral("overlays/ovl-orphan.svg"),
                                  QStringLiteral("templates/old.png")}));
}

TEST(UnusedWorkspaceFiles, AReferenceSpelledDifferentlyStillCounts)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString file = QDir(tmp.path()).filePath(QStringLiteral("overlays/ovl-a.svg"));
    write(file, "used");

    // The same file reached through a detour and native separators.
    const QString detour = QDir::toNativeSeparators(QDir(tmp.path()).filePath(QStringLiteral("overlays/../overlays/ovl-a.svg")));
    EXPECT_TRUE(unusedWorkspaceFiles(tmp.path(), {detour}).isEmpty());
}

TEST(WorkspaceFontFiles, FontsInAnyCaseAndNothingElse)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir d(tmp.path());
    const auto at = [&d](const char* rel) { return d.filePath(QString::fromLatin1(rel)); };

    write(at("fonts/Comic.ttf"), "a");
    write(at("fonts/LOUD.OTF"), "b");
    write(at("fonts/Family.ttc"), "c");
    write(at("fonts/OFL.txt"), "the licence travels, but is no font");
    write(at("fonts/nested/Deep.ttf"), "not recursive");
    write(at("Stray.ttf"), "beside the workspace, not in fonts/");

    QStringList names;
    for (const QString& p : workspaceFontFiles(tmp.path()))
        names << d.relativeFilePath(p);
    names.sort(Qt::CaseInsensitive);
    EXPECT_EQ(names, (QStringList{QStringLiteral("fonts/Comic.ttf"), QStringLiteral("fonts/Family.ttc"),
                                  QStringLiteral("fonts/LOUD.OTF")}));
    EXPECT_TRUE(workspaceFontFiles(d.filePath(QStringLiteral("no-such-folder"))).isEmpty());
}

TEST(InstalledFontFiles, AFamilyIsFoundByItsFilesAndAnUnknownOneIsNot)
{
#ifdef Q_OS_WIN
    // Arial ships with every Windows, in several styles and one file per style.
    const QStringList arial = installedFontFiles(QStringLiteral("Arial"));
    EXPECT_GE(arial.size(), 2);
    const bool hasRegular = std::any_of(arial.begin(), arial.end(), [](const QString& p) {
        return QFileInfo(p).fileName().compare(QStringLiteral("arial.ttf"), Qt::CaseInsensitive) == 0;
    });
    EXPECT_TRUE(hasRegular) << arial.join(QLatin1Char('\n')).toStdString();
    for (const QString& p : arial)
        EXPECT_TRUE(QFileInfo::exists(p)) << p.toStdString();
#endif
    EXPECT_TRUE(installedFontFiles(QStringLiteral("No Such Family 7f3a")).isEmpty());
}
