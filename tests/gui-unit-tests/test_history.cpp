/**
 * \file test_history.cpp
 * \brief The undo history (history.hpp): what an edit records, how a step undoes and redoes, how edits nest,
 *        how a step outlives its scope's owner, and that an undeclared change is caught.
 */

#include "history.hpp"

#include <QStringList>
#include <QUndoStack>

#include <gtest/gtest.h>

#include <memory>

namespace {

//! A scope over one int, recording what it is asked to restore.
class IntScope : public ScopeOf<int>
{
public:
    IntScope(const char* id, int& value, QStringList* restored = nullptr)
        : m_id(QLatin1String(id)), m_value(value), m_restored(restored)
    {
    }
    [[nodiscard]] QString id() const override { return m_id; }

    int      restores = 0;
    QVariant note;

protected:
    [[nodiscard]] int state() const override { return m_value; }
    void install(const int&, const int& to, const QVariant& n) override
    {
        m_value = to;
        ++restores;
        note = n;
        if (m_restored)
            *m_restored << m_id;
    }

private:
    QString      m_id;
    int&         m_value;
    QStringList* m_restored;
};

} // namespace

TEST(History, AnEditThatChangesNothingRecordsNothing)
{
    History  history;
    int      value = 1;
    IntScope scope("a", value);
    history.add(&scope);
    EXPECT_FALSE(history.edit(QStringLiteral("nothing"), {{"a"}}, [&] { value = 1; }));
    EXPECT_EQ(history.stack()->count(), 0);
}

TEST(History, AStepIsAppliedWhenPushedAndUndoesAndRedoes)
{
    constexpr int k_where = 3;   // what the edit says about itself, handed back on restore
    History  history;
    int      value = 1;
    IntScope scope("a", value);
    history.add(&scope);
    EXPECT_TRUE(history.edit(QStringLiteral("two"), {{"a", k_where}}, [&] { value = 2; }));
    EXPECT_EQ(history.stack()->count(), 1);
    EXPECT_EQ(history.stack()->undoText(), QStringLiteral("two"));
    EXPECT_EQ(scope.restores, 0) << "the push's redo is swallowed: the edit is already applied";

    history.stack()->undo();
    EXPECT_EQ(value, 1);
    EXPECT_EQ(scope.note.toInt(), k_where);
    history.stack()->redo();
    EXPECT_EQ(value, 2);
    EXPECT_EQ(scope.restores, 2);
}

TEST(History, EditsInsideAnEditAreOneStep)
{
    History     history;
    QStringList restored;
    int         a = 1, b = 1;
    IntScope    sa("a", a, &restored), sb("b", b, &restored);
    history.add(&sa);
    history.add(&sb);
    // An outer edit that declares nothing, two inner ones that declare a scope each: one Ctrl+Z.
    history.edit(QStringLiteral("both"), {}, [&] {
        EXPECT_FALSE(history.edit(QStringLiteral("a"), {{"a"}}, [&] { a = 2; })) << "joined, not pushed";
        history.edit(QStringLiteral("b"), {{"b"}}, [&] { b = 2; });
    });
    ASSERT_EQ(history.stack()->count(), 1);
    EXPECT_EQ(history.stack()->undoText(), QStringLiteral("both"));

    history.stack()->undo();
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(restored, (QStringList{QStringLiteral("b"), QStringLiteral("a")})) << "undone in reverse order";
    restored.clear();
    history.stack()->redo();
    EXPECT_EQ(restored, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

TEST(History, AStepSkipsAScopeTakenOffAndRestoresOneAddedAgain)
{
    History history;
    int     value = 1;
    auto    first = std::make_unique<IntScope>("a", value);
    history.add(first.get());
    history.edit(QStringLiteral("two"), {{"a"}}, [&] { value = 2; });

    // Its owner goes (a dock closed): the step does nothing rather than reach into it.
    history.remove(first.get());
    first.reset();
    history.stack()->undo();
    EXPECT_EQ(value, 2);
    history.stack()->redo();

    // A new owner puts the scope back under its id (the dock opened again): the step finds it.
    IntScope second("a", value);
    history.add(&second);
    history.stack()->undo();
    EXPECT_EQ(value, 1);
}

TEST(History, AChangeToAScopeTheEditDidNotDeclareIsReported)
{
    History  history;
    int      a = 1, b = 1;
    IntScope sa("a", a), sb("b", b);
    history.add(&sa);
    history.add(&sb);
    QStringList reports;
    history.setUndeclaredHandler([&](const QString& message) { reports << message; });

    history.edit(QStringLiteral("sloppy"), {{"a"}}, [&] {
        a = 2;
        b = 2;
    });
    ASSERT_EQ(reports.size(), 1);
    EXPECT_TRUE(reports.first().contains(QStringLiteral("b"))) << qPrintable(reports.first());

    // Declared by an edit inside, the same change is no report.
    reports.clear();
    history.edit(QStringLiteral("tidy"), {{"a"}}, [&] {
        a = 3;
        history.edit(QStringLiteral("b too"), {{"b"}}, [&] { b = 3; });
    });
    EXPECT_TRUE(reports.isEmpty());
}

TEST(History, KeepsItsDepth)
{
    History  history;
    int      value = 0;
    IntScope scope("a", value);
    history.add(&scope);
    for (int i = 1; i <= History::k_depth + 5; ++i)
        history.edit(QStringLiteral("step"), {{"a"}}, [&] { value = i; });
    EXPECT_EQ(history.stack()->count(), History::k_depth);
}
