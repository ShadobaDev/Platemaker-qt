// What OBJECT STATE is bound to for each kind of selection, and where its answer is written: to the objects it
// was bound to, with the size and the tail tips they hold now. QtCore only, so tested without a scene.

#include <gtest/gtest.h>

#include "objects/panelbinder.hpp"
#include "objectparts.hpp"

using StripEdit::ElementRef;
using StripEdit::PanelBinder;
using StripEdit::SelectionModel;

namespace {

//! A tiny strip: balloons "a" (two tails) and "b" (one), and what was last written to it.
struct Strip
{
    QHash<QString, Composition> objects;
    QStringList                 writtenTo;
    QList<Composition>          written;
    SelectionModel              selection;
    PanelBinder                 binder;

    Strip()
        : binder(selection,
                 {[this](const QString& uid) {
                      return objects.contains(uid) ? std::optional<Composition>(objects.value(uid)) : std::nullopt;
                  },
                  [this](const ElementRef& t) {
                      const QList<BalloonTail>& tails = objects.value(t.object).balloon->tails;
                      for (int i = 0; i < tails.size(); ++i)
                          if (tails.at(i).id == t.element)
                              return i;
                      return -1;
                  },
                  [] { return std::optional<Platemaker::Models::BlendMode>(); },
                  [] { return std::optional<double>(); },
                  [this](const QStringList& uids, const QList<Composition>& all, bool) {
                      writtenTo = uids;
                      written   = all;
                  }})
    {
        Composition a = balloonObject();
        setTails(a, {Tail{}, Tail{}});
        Composition b = balloonObject();
        setTails(b, {Tail{}});
        objects.insert("a", a);
        objects.insert("b", b);
    }

    void select(const QStringList& uids, const QList<ElementRef>& tails = {})
    {
        selection.select(uids, tails, [this](const QString& uid, int element) {
            if (!objects.contains(uid))
                return false;
            if (element == 0)
                return true;
            for (const BalloonTail& t : objects.value(uid).balloon->tails)
                if (t.id == element)
                    return true;
            return false;
        });
        binder.bind();
    }

    [[nodiscard]] ElementRef tail(const QString& uid, int index) const
    {
        return {uid, objects.value(uid).balloon->tails.at(index).id};
    }
};

} // namespace

TEST(PanelBinder, OneObjectIsBoundAsItIsNow)
{
    Strip       s;
    Composition shown;
    QObject::connect(&s.binder, &PanelBinder::boundToRecord, [&](const Composition& c) { shown = c; });
    s.select({"a"});
    EXPECT_TRUE(shown == s.objects.value("a"));
    EXPECT_EQ(s.binder.subjects(), QStringList{"a"});
}

TEST(PanelBinder, AnAnswerKeepsTheSizeAndTipsTheObjectHolds)
{
    Strip s;
    s.select({"a"});
    Composition answer                  = s.objects.value("a");
    textOf(answer).body                 = QStringLiteral("said");
    boxOf(s.objects["a"])               = QSize(333, 111);
    tailsOf(s.objects["a"])[0].tail.tip = QPointF(7, 9);
    s.binder.applyEdits({answer}, true);
    ASSERT_EQ(s.written.size(), 1);
    EXPECT_EQ(s.writtenTo, QStringList{"a"});
    EXPECT_EQ(textOf(s.written.first()).body, QStringLiteral("said"));
    EXPECT_EQ(boxOf(s.written.first()), QSize(333, 111));
    EXPECT_EQ(tailsOf(s.written.first()).at(0).tail.tip, QPointF(7, 9));
}

TEST(PanelBinder, EachKindOfSelectionIsBoundAsItsOwn)
{
    Strip s;
    int   tailIndex = -1, mixed = -1, several = -1, nothing = 0;
    QList<QPair<int, int>> tails;
    QObject::connect(&s.binder, &PanelBinder::boundToTail, [&](const Composition&, int i) { tailIndex = i; });
    QObject::connect(&s.binder, &PanelBinder::boundToTails,
                     [&](const QList<Composition>&, const QList<QPair<int, int>>& t) { tails = t; });
    QObject::connect(&s.binder, &PanelBinder::boundToMixed, [&](int n) { mixed = n; });
    QObject::connect(&s.binder, &PanelBinder::boundToRecords, [&](const QList<Composition>& all) { several = int(all.size()); });
    QObject::connect(&s.binder, &PanelBinder::boundToNothing, [&] { ++nothing; });

    s.select({}, {s.tail("a", 1)});
    EXPECT_EQ(tailIndex, 1);
    EXPECT_EQ(s.binder.subjects(), QStringList{"a"});

    s.select({}, {s.tail("b", 0), s.tail("a", 1)});
    EXPECT_EQ(tails, (QList<QPair<int, int>>{{0, 0}, {1, 1}}));
    EXPECT_EQ(s.binder.subjects(), (QStringList{"b", "a"}));

    s.select({"b"}, {s.tail("a", 0)});
    EXPECT_EQ(mixed, 2);
    EXPECT_TRUE(s.binder.subjects().isEmpty()) << "nothing editable in common: an answer writes nothing";

    s.select({"a", "b"});
    EXPECT_EQ(several, 2);

    s.select({});
    EXPECT_EQ(nothing, 1);
    EXPECT_TRUE(s.binder.subjects().isEmpty());
}
