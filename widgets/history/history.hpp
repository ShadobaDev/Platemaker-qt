#ifndef HISTORY_HPP
#define HISTORY_HPP

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>

#include <any>
#include <functional>
#include <initializer_list>
#include <optional>
#include <vector>

class QUndoStack;

/**
 * \brief One part of the document a history step can restore: the workspace's profiles, a project's pages,
 *        a project's lettering.
 *
 * Scopes are **disjoint**: every piece of state belongs to exactly one, so restoring one never treads on
 * another. A scope hands out its whole state as a value only it reads (a memento), compares two, and puts
 * one back; History stores what it hands out and never looks inside. Write a scope through ScopeOf, which
 * does the type erasure.
 */
class HistoryScope
{
public:
    virtual ~HistoryScope() = default;

    //! What an edit names it by (History::edit()); one per History.
    [[nodiscard]] virtual QString id() const = 0;
    //! Its whole state now.
    [[nodiscard]] virtual std::any capture() const = 0;
    //! Whether two of its states are the same: an edit that leaves it the same records nothing.
    [[nodiscard]] virtual bool same(const std::any& a, const std::any& b) const = 0;
    /**
     * \brief Puts @p to back, the document holding @p from, and shows where it changed. @p note is what the
     *        edit said about itself (History::Touch::note).
     *
     * Installs state and says what changed, **nothing else**: what is derived from the state (a bubble's file)
     * follows it, made by whoever derives it, and a failure there is reported, not swallowed.
     */
    virtual void restore(const std::any& from, const std::any& to, const QVariant& note) = 0;
};

//! A HistoryScope whose state is a @p State: a copyable value with operator==.
template <class State>
class ScopeOf : public HistoryScope
{
protected:
    [[nodiscard]] virtual State state() const = 0;
    virtual void install(const State& from, const State& to, const QVariant& note) = 0;

private:
    [[nodiscard]] std::any capture() const final { return state(); }
    [[nodiscard]] bool same(const std::any& a, const std::any& b) const final
    {
        return std::any_cast<const State&>(a) == std::any_cast<const State&>(b);
    }
    void restore(const std::any& from, const std::any& to, const QVariant& note) final
    {
        install(std::any_cast<const State&>(from), std::any_cast<const State&>(to), note);
    }
};

/**
 * \brief One undo timeline: a QUndoStack, the scopes that live on it, and the one way to record a step.
 *
 * Every edit goes through edit(): it captures the scopes the edit declares, runs it, captures them again,
 * and pushes one step holding those that changed, or nothing. A step is already applied when pushed, so
 * the redo the push fires is swallowed. An edit inside another joins it, so an action made of others (pages
 * removed with their bubbles) is one Ctrl+Z without anyone opening a group.
 *
 * A step names its scopes by id and finds them here when undone, so it outlives the widget that recorded
 * it: a scope unregistered (its dock closed) is skipped, and one registered again under the same id (the
 * dock reopened) is restored.
 *
 * Every edit is also checked against the scopes it did not declare: one that changed is a bug in the edit
 * (its step would not undo all it did), reported through setUndeclaredHandler(), by default an assertion.
 */
class History : public QObject
{
    Q_OBJECT

public:
    //! How many steps a timeline keeps.
    static constexpr int k_depth = 20;

    //! @name The application's scopes, by id
    //! @{
    static constexpr char k_workspace[] = "workspace";   //!< Profiles, templates, project names (MainWindow).
    static constexpr char k_project[]   = "project";     //!< A project's pages, links, outputs, grade (Project).
    static constexpr char k_lettering[] = "lettering";   //!< A project's text & bubbles (Project).
    //! @}

    //! A scope an edit changes, and what it says about the change, handed back to the scope's restore().
    struct Touch
    {
        const char* scope;
        QVariant    note = {};
    };

    explicit History(QObject* parent = nullptr);
    ~History() override;

    [[nodiscard]] QUndoStack* stack() const { return m_stack; }

    //! Makes @p scope restorable on this timeline, under its id; one scope per id. Not owned.
    void add(HistoryScope* scope);
    //! Takes @p scope off: steps naming it skip it until one is added again under its id.
    void remove(HistoryScope* scope);

    /**
     * \brief Runs @p mutate as one step, labelled @p label, of the scopes in @p touched.
     * \return Whether a step was pushed: never inside another edit, which records it with its own.
     */
    bool edit(const QString& label, std::initializer_list<Touch> touched, const std::function<void()>& mutate);

    //! What is called with a message when an edit changed a scope it did not declare.
    void setUndeclaredHandler(std::function<void(const QString&)> handler) { m_undeclared = std::move(handler); }

private:
    class Step;
    struct Part
    {
        QString  scope;
        QVariant note;
        std::any before;
        std::any after;
    };
    struct Open
    {
        QString           label;
        std::vector<Part> parts;    //!< The declared scopes, as they were when first declared.
        std::vector<Part> guards;   //!< Every other scope, as it was when the edit began.
    };

    [[nodiscard]] HistoryScope* scope(const QString& id) const { return m_scopes.value(id); }

    QUndoStack*                         m_stack = nullptr;
    QHash<QString, HistoryScope*>       m_scopes;
    std::optional<Open>                 m_open;   //!< The edit being run, while one is.
    std::function<void(const QString&)> m_undeclared;
};

#endif // HISTORY_HPP
