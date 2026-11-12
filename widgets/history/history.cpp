#include "history.hpp"

#include <QUndoCommand>
#include <QUndoStack>
#include <QtGlobal>

#include <algorithm>
#include <utility>

/**
 * \brief One step: the parts of the scopes it changed, each before and after. Undone in the reverse of the
 *        order they were declared, redone in it.
 */
class History::Step : public QUndoCommand
{
public:
    Step(History* history, const QString& label, std::vector<Part> parts)
        : QUndoCommand(label), m_history(history), m_parts(std::move(parts))
    {
    }

    void undo() override
    {
        for (auto it = m_parts.rbegin(); it != m_parts.rend(); ++it)
            if (HistoryScope* s = m_history->scope(it->scope))
                s->restore(it->after, it->before, it->note);
    }

    void redo() override
    {
        // The push fires a redo, and the edit is already applied: only a redo after an undo restores.
        if (m_pushed) {
            m_pushed = false;
            return;
        }
        for (const Part& p : m_parts)
            if (HistoryScope* s = m_history->scope(p.scope))
                s->restore(p.before, p.after, p.note);
    }

private:
    History*          m_history;   //!< Outlives the step: the stack the step is on is its child.
    std::vector<Part> m_parts;
    bool              m_pushed = true;
};

History::History(QObject* parent)
    : QObject(parent), m_stack(new QUndoStack(this))
{
    m_stack->setUndoLimit(k_depth);
    m_undeclared = [](const QString& message) {
        qCritical("%s", qPrintable(message));
        Q_ASSERT_X(false, "History::edit", qPrintable(message));
    };
}

History::~History() = default;

void History::add(HistoryScope* scope)
{
    Q_ASSERT_X(!m_scopes.contains(scope->id()), "History::add", "a scope with this id is already here");
    m_scopes.insert(scope->id(), scope);
}

void History::remove(HistoryScope* scope)
{
    if (m_scopes.value(scope->id()) == scope)
        m_scopes.remove(scope->id());
}

bool History::edit(const QString& label, std::initializer_list<Touch> touched, const std::function<void()>& mutate)
{
    const bool outer = !m_open;
    if (outer)
        m_open = Open{label, {}, {}};
    const auto declared = [this](const QString& id) {
        return std::any_of(m_open->parts.begin(), m_open->parts.end(), [&](const Part& p) { return p.scope == id; });
    };
    for (const Touch& t : touched) {
        const QString id = QLatin1String(t.scope);
        HistoryScope* s  = scope(id);
        Q_ASSERT_X(s, "History::edit", "an edit names a scope this timeline does not have");
        if (s && !declared(id))
            m_open->parts.push_back({id, t.note, s->capture(), {}});
    }
    if (outer)
        for (auto it = m_scopes.cbegin(); it != m_scopes.cend(); ++it)
            if (!declared(it.key()))
                m_open->guards.push_back({it.key(), {}, it.value()->capture(), {}});

    if (!outer) {
        mutate();
        return false;
    }

    // Closed whatever happens: an edit that throws records nothing, and the next one starts fresh.
    struct Close
    {
        std::optional<Open>& open;
        ~Close() { open.reset(); }
    } close{m_open};
    mutate();
    Open done = std::move(*m_open);

    for (const Part& g : done.guards) {
        const bool listed = std::any_of(done.parts.begin(), done.parts.end(), [&](const Part& p) { return p.scope == g.scope; });
        HistoryScope* s = scope(g.scope);
        if (!listed && s && !s->same(g.before, s->capture()))
            m_undeclared(QStringLiteral("\"%1\" changed the %2 scope without declaring it: its undo would not "
                                        "put that back").arg(label, g.scope));
    }

    std::vector<Part> changed;
    for (Part& p : done.parts) {
        HistoryScope* s = scope(p.scope);
        if (!s)
            continue;
        p.after = s->capture();
        if (!s->same(p.before, p.after))
            changed.push_back(std::move(p));
    }
    if (changed.empty())
        return false;   // nothing changed: no step in the history
    m_stack->push(new Step(this, done.label, std::move(changed)));
    return true;
}
