#include "workspacefolder.hpp"

#include <QDir>
#include <QFileInfo>

QStringList workspacesInFolder(const QString& dir)
{
    const QDir d(dir);
    QStringList out;
    for (const QString& name :
         d.entryList({QString::fromLatin1(k_workspaceFilePattern)}, QDir::Files, QDir::Name))
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
