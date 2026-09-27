#include "workspacelock.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSysInfo>

namespace {

//! One process lock per workspace folder, named by the folder rather than placed in it. Canonical where the
//! folder exists, so two spellings of one folder share a lock.
QString processLockName(const QString& folder)
{
    const QFileInfo fi(folder);
    const QString   key = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
    return QString::fromLatin1(
               QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16))
         + QStringLiteral(".lock");
}

} // namespace

WorkspaceLock::WorkspaceLock(QString workspaceFolder, QString processLockDir)
    : m_folder(std::move(workspaceFolder))
    , m_processLockDir(std::move(processLockDir))
{
}

WorkspaceLock::~WorkspaceLock()
{
    // Only our own marker is ours to delete. After a takeover it names the other computer, and deleting it
    // would hand the folder to whoever came next while that computer is still writing to it.
    if (m_marked && stillOurs())
        QFile::remove(markerPath());
    // The process lock releases itself.
}

QString WorkspaceLock::markerPath() const
{
    return QDir(m_folder).filePath(QString::fromLatin1(k_markerName));
}

QByteArray WorkspaceLock::markerFor(const QString& host, qint64 pid, const QDateTime& since)
{
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("host"), host},
                             {QStringLiteral("pid"), static_cast<double>(pid)},
                             {QStringLiteral("since"), since.toString(Qt::ISODate)},
                         })
        .toJson(QJsonDocument::Indented);
}

WorkspaceLock::Holder WorkspaceLock::parseMarker(const QByteArray& bytes)
{
    const QJsonObject j = QJsonDocument::fromJson(bytes).object();
    Holder h;
    h.host  = j.value(QStringLiteral("host")).toString();
    h.pid   = static_cast<qint64>(j.value(QStringLiteral("pid")).toDouble());
    h.since = QDateTime::fromString(j.value(QStringLiteral("since")).toString(), Qt::ISODate);
    return h;
}

bool WorkspaceLock::writeMarker()
{
    // Written whole or not at all, and closed at once — the property that lets a synced drive carry it.
    QSaveFile f(markerPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(markerFor(QSysInfo::machineHostName(), QCoreApplication::applicationPid(),
                      QDateTime::currentDateTime()));
    return f.commit();
}

WorkspaceLock::Outcome WorkspaceLock::acquire()
{
    // --- This machine first: another window here, alive or not. -----------------------------------------
    if (QDir().mkpath(m_processLockDir)) {
        m_processLock = std::make_unique<QLockFile>(QDir(m_processLockDir).filePath(processLockName(m_folder)));
        // A long-held lock (the Qt pattern for a document): never stale by age, only by its process being
        // gone — which tryLock() checks, and clears, by itself.
        m_processLock->setStaleLockTime(0);
        if (!m_processLock->tryLock()) {
            if (m_processLock->error() == QLockFile::LockFailedError) {
                QString app;
                m_holder = {};
                (void)m_processLock->getLockInfo(&m_holder.pid, &m_holder.host, &app);
                m_processLock.reset();
                return Outcome::OpenHere;
            }
            m_processLock.reset();   // no local lock to be had; the marker can still speak for us
        }
    }

    // --- Then everyone else: the marker in the folder. ----------------------------------------------------
    QFile existing(markerPath());
    if (existing.open(QIODevice::ReadOnly)) {
        const Holder h = parseMarker(existing.readAll());
        existing.close();
        // A marker from this machine whose window is gone (the process lock above was free) is a crash left
        // behind: ours to overwrite. One from another computer is not ours to judge.
        if (!h.host.isEmpty() && h.host != QSysInfo::machineHostName()) {
            m_holder = h;
            return Outcome::OpenElsewhere;
        }
    }

    m_marked = writeMarker();
    return m_marked ? Outcome::Acquired : Outcome::Unavailable;
}

bool WorkspaceLock::takeOver()
{
    m_marked = writeMarker();
    return m_marked;
}

bool WorkspaceLock::stillOurs()
{
    if (!m_marked)
        return true;

    QFile f(markerPath());
    if (!f.open(QIODevice::ReadOnly))
        return writeMarker();   // deleted by hand: nobody else claimed it, so claim it again

    const Holder h = parseMarker(f.readAll());
    f.close();
    if (h.host == QSysInfo::machineHostName() && h.pid == QCoreApplication::applicationPid())
        return true;
    if (h.host.isEmpty())
        return writeMarker();   // not a claim anybody could have made — a truncated or foreign file
    m_holder = h;
    return false;
}
