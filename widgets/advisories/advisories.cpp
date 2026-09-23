#include "advisories.hpp"

#include <algorithm>

namespace {

//! Whether two advisories say the same thing. The action is left out deliberately — a `std::function`
//! cannot be compared, and two advisories that read identically offer the same way out by construction.
[[nodiscard]] bool saysTheSame(const Advisory& a, const Advisory& b)
{
    return a.level == b.level && a.text == b.text && a.detail == b.detail
        && a.actionText == b.actionText && a.resolveText == b.resolveText
        && a.projectUid == b.projectUid;
}

} // namespace

void Advisories::raise(const QString& key, Advisory advisory)
{
    const auto it = m_standing.constFind(key);
    if (it != m_standing.constEnd() && saysTheSame(it.value(), advisory))
        return;   // the same condition, re-evaluated — nothing has happened worth telling anyone

    m_standing.insert(key, std::move(advisory));
    emit changed();
}

void Advisories::clear(const QString& key)
{
    if (m_standing.remove(key) > 0)
        emit changed();
}

void Advisories::clearProject(const QString& projectUid)
{
    const qsizetype removed = m_standing.removeIf([&](auto it) {
        return it.value().projectUid == projectUid;
    });
    if (removed > 0)
        emit changed();
}

void Advisories::clearAll()
{
    if (m_standing.isEmpty())
        return;
    m_standing.clear();
    emit changed();
}

QList<Advisory> Advisories::forProject(const QString& projectUid) const
{
    QList<Advisory> out;
    for (const Advisory& a : m_standing)
        if (a.projectUid.isEmpty() || a.projectUid == projectUid)
            out.append(a);

    // Worst first, then by text. A hash has no order to inherit, so without the tiebreak two advisories
    // of one level would swap places between rebuilds and the status bar would shuffle for no reason.
    std::sort(out.begin(), out.end(), [](const Advisory& a, const Advisory& b) {
        if (a.level != b.level)
            return static_cast<int>(a.level) > static_cast<int>(b.level);
        return a.text < b.text;
    });
    return out;
}

bool Advisories::hasError(const QString& projectUid) const
{
    return std::any_of(m_standing.cbegin(), m_standing.cend(), [&](const Advisory& a) {
        return a.level == Advisory::Level::Error
            && (a.projectUid.isEmpty() || a.projectUid == projectUid);
    });
}
