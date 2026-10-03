#include "workspacefolder.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <utility>

QStringList workspacesInFolder(const QString& dir)
{
    const QDir d(dir);
    QStringList out;
    for (const QString& name :
         d.entryList({QString::fromLatin1(k_workspaceFilePattern)}, QDir::Files, QDir::Name))
        out << d.absoluteFilePath(name);
    return out;
}

QStringList workspaceFontFiles(const QString& folder)
{
    const QDir  d(QDir(folder).filePath(QString::fromLatin1(k_workspaceFontsFolder)));
    QStringList out;
    // Name filters ignore case unless QDir::CaseSensitive is asked for, on every platform.
    for (const QString& name : d.entryList(
             {QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.ttc")}, QDir::Files, QDir::Name))
        out << d.absoluteFilePath(name);
    return out;
}

QStringList otherWorkspacesBeside(const QString& workspacePath)
{
    const QFileInfo self(workspacePath);
    QStringList others = workspacesInFolder(self.absolutePath());
    // QFileInfo compares canonical paths, and case-insensitively where the file system is — so a path
    // typed differently from how the folder listing spells it still counts as the same file.
    others.removeIf([&self](const QString& p) { return QFileInfo(p) == self; });
    return others;
}

namespace {

bool sameBytes(const QString& a, const QString& b)
{
    QFile fa(a), fb(b);
    if (fa.size() != fb.size() || !fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    return fa.readAll() == fb.readAll();
}

//! The name a new overlay file with these bytes would get: its kind (`ovl`, `art`) and a content hash.
QString contentName(const QString& source)
{
    QFile f(source);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QFileInfo fi(source);
    const QString   kind = fi.fileName().section(QLatin1Char('-'), 0, 0);
    const QString   sha  = QString::fromLatin1(
        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex().left(16));
    return kind + QLatin1Char('-') + sha + QLatin1Char('.') + fi.suffix();
}

} // namespace

bool copyUnlessIdentical(const QString& source, const QString& dest)
{
    if (QFileInfo::exists(dest))
        return sameBytes(source, dest);
    return QFile::copy(source, dest);
}

bool collectOverlayFiles(std::vector<Platemaker::Models::StripOverlay>& overlays,
                         const ObjectRecord::Map&                              records,
                         const QString&                                  overlaysDir,
                         QString*                                        failed)
{
    const QDir      home(overlaysDir);
    const QFileInfo homeInfo(overlaysDir);
    const auto      fail = [failed](const QString& what) {
        if (failed)
            *failed = what;
        return false;
    };

    // Copies first, paths after: nothing below the loop runs unless every file made it.
    std::vector<std::pair<std::size_t, QString>> repointed;
    for (std::size_t i = 0; i < overlays.size(); ++i) {
        const QString source = QString::fromStdString(overlays[i].assetPath);
        if (source.isEmpty())
            continue;
        const QFileInfo sourceInfo(source);
        if (QFileInfo(sourceInfo.absolutePath()) == homeInfo)
            continue;   // already in this folder

        const auto     rec     = records.constFind(QString::fromStdString(overlays[i].uid));
        const bool     hasRec  = rec != records.constEnd();
        const ObjectRecord record  = hasRec ? *rec : ObjectRecord{};

        if (!sourceInfo.exists()) {
            // Nothing to copy. A balloon is drawn from its record, so its file belongs here and is written
            // here the next time it is; a picture has nothing to be rebuilt from and stays where it was.
            if (hasRec && !record.isArtwork())
                repointed.emplace_back(i, home.filePath(sourceInfo.fileName()));
            continue;
        }

        // A lettered picture: the overlay is the wrapper, and the picture sits beside it under the name its
        // record gives. That name is load-bearing, so the picture is never renamed.
        const QString picture = record.isArtwork()
                                    ? QDir(sourceInfo.absolutePath()).filePath(record.artwork)
                                    : QString{};
        if (!picture.isEmpty() && QFileInfo::exists(picture)
            && !copyUnlessIdentical(picture, home.filePath(record.artwork)))
            return fail(picture);

        QString dest = home.filePath(sourceInfo.fileName());
        if (!copyUnlessIdentical(source, dest)) {
            // Only a name clash is worth a second try, and only for a file no record names.
            if (!QFileInfo::exists(dest) || (!picture.isEmpty() && QFileInfo(source) == QFileInfo(picture)))
                return fail(source);
            const QString renamed = contentName(source);
            if (renamed.isEmpty())
                return fail(source);
            dest = home.filePath(renamed);
            if (!copyUnlessIdentical(source, dest))
                return fail(source);
        }
        repointed.emplace_back(i, dest);
    }

    for (const auto& [i, path] : repointed)
        overlays[i].assetPath = path.toStdString();
    return true;
}

QStringList unusedWorkspaceFiles(const QString& folder, const QStringList& referenced)
{
    QSet<QString> used;
    for (const QString& path : referenced)
        used.insert(QFileInfo(path).canonicalFilePath());   // "" for a missing file matches nothing

    QStringList unused;
    const auto scan = [&](const QString& sub, const QStringList& patterns) {
        const QDir dir(QDir(folder).filePath(sub));
        for (const QFileInfo& fi : dir.entryInfoList(patterns, QDir::Files, QDir::Name))
            if (!used.contains(fi.canonicalFilePath()))
                unused << fi.absoluteFilePath();
    };
    scan(QStringLiteral("overlays"), {QStringLiteral("ovl-*.svg"), QStringLiteral("art-*")});
    scan(QStringLiteral("templates"), {QStringLiteral("*")});
    return unused;
}
