#ifndef ADVISORIES_H
#define ADVISORIES_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

/**
 * @brief One thing that is set up in a way that will not do what the artist meant.
 *
 * **The action travels with the warning.** *"grade not run"* is an annoyance; *"grade not run —
 * **turn the step on**"* is help. An advisory that cannot say what to do about itself is usually not
 * worth raising.
 *
 * An advisory is a **condition**, derived from state — whoever raised it re-evaluates it and it
 * disappears when the artist fixes the cause. Something that merely *happened* ("converted 2 texts to
 * bubbles") is not one: nothing can make it stop being true, so nothing can clear it. Those go to the
 * status bar's temporary message instead, which needs no registry because it expires by itself.
 */
struct Advisory
{
    enum class Level {
        Info,      //!< A remark.
        Warning,   //!< Worth knowing; the render proceeds.
        Error,     //!< The render stops and asks.
    };

    Level   level = Level::Info;
    QString text;         //!< Four words, for the badge.
    QString detail;       //!< The tooltip: what is wrong, and why.
    QString actionText;   //!< Optional — what to do about it. Names the button, not the problem.
    std::function<void()> action;   //!< What @c actionText does. Absent means there is nothing to offer.

    /**
     * @brief Optional — a way to make the condition go away here and now, which may destroy something.
     *
     * Kept apart from @c action because the two are offered differently. An action only takes the
     * artist somewhere, so it is one click on a chip. A resolution can delete work, so it is **only
     * ever offered in a question** — the render gate — and never sits on a chip one stray click away.
     */
    QString resolveText;
    std::function<void()> resolve;   //!< What @c resolveText does.

    /**
     * @brief Which project this is about; empty means the application itself.
     *
     * The status bar shows one project's advisories at a time, and the render gate asks about one
     * project, so an advisory that could not say which project it meant would be reported against
     * every project or none.
     */
    QString projectUid;
};

/**
 * @brief Every standing advisory, for the whole application.
 *
 * **Not part of the tool framework**, and the reason is concrete: a tool only notices that the grade
 * will not run once you are in the strip editor, but the condition it reports is set in the Workflow
 * tab. Whoever can observe the condition raises it — the workflow map, the strip editor's tools, the
 * render preflight — and the status bar subscribes without knowing about any of them.
 *
 * Keyed, so a condition re-evaluated on every edit replaces its own entry instead of accumulating
 * duplicates. Re-raising an advisory that has not actually changed says nothing, which matters because
 * the re-evaluation runs on every keystroke that reaches a project.
 */
class Advisories : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    //! Raise or replace the advisory under @p key. Silent when it is the one already standing there.
    void raise(const QString& key, Advisory advisory);

    //! Withdraw @p key's advisory. Silent when nothing stands there.
    void clear(const QString& key);

    /**
     * @brief Withdraw everything about @p projectUid — it has been removed, so nothing is true of it.
     *
     * By the advisory's own scope rather than by a list of keys the caller remembers, so a condition
     * added later is withdrawn here on the day it is added rather than on the day someone notices.
     */
    void clearProject(const QString& projectUid);

    //! Withdraw everything. The workspace is closing; no condition in it is still being observed.
    void clearAll();

    /**
     * @brief What stands for @p projectUid, plus everything application-wide, **worst first**.
     *
     * Worst first because the first badge is the one that gets read, and because a render gate that
     * has to name one reason should name the one that stopped it.
     */
    [[nodiscard]] QList<Advisory> forProject(const QString& projectUid) const;

    //! Whether anything at Error level stands for @p projectUid — the render gate's whole test.
    [[nodiscard]] bool hasError(const QString& projectUid) const;

signals:
    //! Something was raised, replaced or withdrawn. Carries nothing: a subscriber re-reads.
    void changed();

private:
    QHash<QString, Advisory> m_standing;
};

#endif // ADVISORIES_H
