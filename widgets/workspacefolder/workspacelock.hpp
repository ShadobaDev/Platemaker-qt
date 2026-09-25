#ifndef WORKSPACELOCK_HPP
#define WORKSPACELOCK_HPP

#include <QDateTime>
#include <QString>

#include <memory>

class QLockFile;

/**
 * @brief Who may write to a workspace folder right now — and how that is found out when it changes.
 *
 * **Two files, because one cannot do both jobs.** A `QLockFile` is what tells a live holder from a dead
 * one on this machine (it records the process and checks it is still running, so a lock left by a crash
 * or a reset goes away by itself). But it keeps its file open for as long as it holds it, and an open lock
 * file can be neither written nor deleted by anyone else — measured, both on a local disk and on the
 * Google Drive `G:`. Put in the workspace folder, it would stop a synced drive from ever delivering a
 * takeover from another computer: the holder would go on reading its own, unchanged file. So:
 *
 *  - the **process lock** is a `QLockFile` in a local folder the caller names (application data), one per
 *    workspace folder, never synced — it answers "is this open in another window here?";
 *  - the **marker** is `.platemaker.lock` in the workspace folder: the holder's machine, process and the
 *    time it took the folder, written and closed at once, so a synced drive carries it both ways. It
 *    answers "is this open on another computer?", and — read again before a write — "is it still mine?".
 *
 * QtCore only, so the rules are tests.
 */
class WorkspaceLock
{
public:
    //! The marker's file name, in the workspace folder. Visible on purpose: a lock left by a computer that
    //! crashed is one the user can see, understand and delete.
    static constexpr char k_markerName[] = ".platemaker.lock";

    enum class Outcome {
        Acquired,       //!< The folder is ours.
        OpenHere,       //!< Another Platemaker on this machine has it open — holder() says which.
        OpenElsewhere,  //!< The marker names another computer — holder() says which; takeOver() is the way in.
        Unavailable,    //!< Nothing could be written (read-only media, full disk): open, but unguarded.
    };

    //! Who holds the folder, as the marker (or the process lock) reports it.
    struct Holder {
        QString   host;
        qint64    pid = 0;
        QDateTime since;   //!< Invalid when the process lock was the one to answer — it records no time.
    };

    /**
     * @param workspaceFolder The folder being locked.
     * @param processLockDir  A local, unsynced folder for the process lock; created when missing.
     */
    WorkspaceLock(QString workspaceFolder, QString processLockDir);
    ~WorkspaceLock();   //!< Lets the folder go — deleting the marker only if it is still ours.

    WorkspaceLock(const WorkspaceLock&)            = delete;
    WorkspaceLock& operator=(const WorkspaceLock&) = delete;

    [[nodiscard]] Outcome acquire();

    /**
     * @brief After OpenElsewhere: claims the folder from the computer the marker names.
     *
     * For a computer that is off, or where Platemaker crashed — the one case that cannot be detected from
     * here. If it is in fact still running, it finds out before its next write (stillOurs()).
     */
    [[nodiscard]] bool takeOver();

    /**
     * @brief Whether the marker still names this process — asked before every write to the folder.
     *
     * True when the folder could not be marked at all (Unavailable): there is nothing to have lost. A marker
     * deleted by hand is written again rather than read as a takeover.
     */
    [[nodiscard]] bool stillOurs();

    [[nodiscard]] bool    isHeld() const { return m_marked; }   //!< False when Unavailable.
    [[nodiscard]] QString folder() const { return m_folder; }
    [[nodiscard]] Holder  holder() const { return m_holder; }   //!< Valid after OpenHere / OpenElsewhere, or a lost stillOurs().

    //! The marker's contents for this process — what acquire() and takeOver() write.
    [[nodiscard]] static QByteArray markerFor(const QString& host, qint64 pid, const QDateTime& since);
    //! Reads a marker; an unreadable or foreign file gives a Holder with an empty host.
    [[nodiscard]] static Holder parseMarker(const QByteArray& bytes);

private:
    [[nodiscard]] QString markerPath() const;
    [[nodiscard]] bool    writeMarker();

    QString                    m_folder;
    QString                    m_processLockDir;
    std::unique_ptr<QLockFile> m_processLock;
    bool                       m_marked = false;
    Holder                     m_holder;
};

#endif // WORKSPACELOCK_HPP
