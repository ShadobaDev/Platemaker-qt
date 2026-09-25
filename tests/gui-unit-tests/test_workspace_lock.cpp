// The workspace lock's cases, one per row of the table users meet: a folder that is free, open in another
// window here, open on another computer, taken over from under its holder, and left behind by a crash.
// "Another computer" is a marker naming another host — exactly what a synced drive would deliver.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>

#include "workspacelock.hpp"

namespace {

//! A workspace folder and a separate process-lock folder, as the application keeps them.
struct Folders {
    QTemporaryDir workspace;
    QTemporaryDir locks;

    [[nodiscard]] QString marker() const
    {
        return QDir(workspace.path()).filePath(QString::fromLatin1(WorkspaceLock::k_markerName));
    }
    void writeMarker(const QString& host, qint64 pid) const
    {
        QFile f(marker());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(WorkspaceLock::markerFor(host, pid, QDateTime::currentDateTime()));
    }
    [[nodiscard]] WorkspaceLock::Holder readMarker() const
    {
        QFile f(marker());
        return f.open(QIODevice::ReadOnly) ? WorkspaceLock::parseMarker(f.readAll()) : WorkspaceLock::Holder{};
    }
};

const QString k_otherHost = QStringLiteral("LAPTOP-SOMEWHERE-ELSE");

} // namespace

TEST(WorkspaceLock, AFreeFolderIsTakenAndGivenBack)
{
    Folders f;
    {
        WorkspaceLock lock(f.workspace.path(), f.locks.path());
        ASSERT_EQ(lock.acquire(), WorkspaceLock::Outcome::Acquired);
        EXPECT_TRUE(lock.isHeld());
        EXPECT_TRUE(lock.stillOurs());
        EXPECT_EQ(f.readMarker().host, QSysInfo::machineHostName());
        EXPECT_EQ(f.readMarker().pid, QCoreApplication::applicationPid());
    }
    EXPECT_FALSE(QFile::exists(f.marker()));   // closing leaves nothing behind
}

TEST(WorkspaceLock, ASecondWindowHereIsTurnedAway)
{
    Folders       f;
    WorkspaceLock first(f.workspace.path(), f.locks.path());
    ASSERT_EQ(first.acquire(), WorkspaceLock::Outcome::Acquired);

    WorkspaceLock second(f.workspace.path(), f.locks.path());
    EXPECT_EQ(second.acquire(), WorkspaceLock::Outcome::OpenHere);
    EXPECT_EQ(second.holder().host, QSysInfo::machineHostName());
    EXPECT_FALSE(second.isHeld());
    EXPECT_TRUE(first.stillOurs());   // being asked did not disturb the holder
}

TEST(WorkspaceLock, AnotherComputerHasToBeTakenOverFrom)
{
    Folders f;
    f.writeMarker(k_otherHost, 4242);

    WorkspaceLock lock(f.workspace.path(), f.locks.path());
    ASSERT_EQ(lock.acquire(), WorkspaceLock::Outcome::OpenElsewhere);
    EXPECT_EQ(lock.holder().host, k_otherHost);
    EXPECT_EQ(f.readMarker().host, k_otherHost);   // asking wrote nothing

    ASSERT_TRUE(lock.takeOver());
    EXPECT_EQ(f.readMarker().host, QSysInfo::machineHostName());
    EXPECT_TRUE(lock.stillOurs());
}

TEST(WorkspaceLock, TheHolderFindsOutItWasTakenOver)
{
    Folders f;
    {
        WorkspaceLock lock(f.workspace.path(), f.locks.path());
        ASSERT_EQ(lock.acquire(), WorkspaceLock::Outcome::Acquired);

        f.writeMarker(k_otherHost, 4242);   // the other computer took over; the drive synced it here
        EXPECT_FALSE(lock.stillOurs());
        EXPECT_EQ(lock.holder().host, k_otherHost);
    }
    // Letting go must not delete a marker that is no longer ours.
    EXPECT_EQ(f.readMarker().host, k_otherHost);
}

TEST(WorkspaceLock, AMarkerLeftByACrashHereIsReclaimed)
{
    Folders f;
    f.writeMarker(QSysInfo::machineHostName(), 1);   // this machine, a window that is gone

    WorkspaceLock lock(f.workspace.path(), f.locks.path());
    ASSERT_EQ(lock.acquire(), WorkspaceLock::Outcome::Acquired);
    EXPECT_EQ(f.readMarker().pid, QCoreApplication::applicationPid());
}

TEST(WorkspaceLock, AMarkerDeletedByHandIsNotATakeover)
{
    Folders       f;
    WorkspaceLock lock(f.workspace.path(), f.locks.path());
    ASSERT_EQ(lock.acquire(), WorkspaceLock::Outcome::Acquired);

    ASSERT_TRUE(QFile::remove(f.marker()));
    EXPECT_TRUE(lock.stillOurs());
    EXPECT_EQ(f.readMarker().host, QSysInfo::machineHostName());   // and it is back
}
