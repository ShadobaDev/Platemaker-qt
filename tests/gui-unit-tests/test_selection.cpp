// What is selected: objects in pick order, a balloon's tails by their element ids, and what each makes the
// selection. QtCore only, so tested without a scene.

#include <gtest/gtest.h>

#include "objects/selection.hpp"

using StripEdit::ElementRef;
using StripEdit::SelectionModel;
using StripEdit::Subject;

namespace {

//! Two balloons, "a" with tails 3 and 4, "b" with tail 7; and a picture "p" with none.
bool exists(const QString& object, int element)
{
    if (element == 0)
        return object == "a" || object == "b" || object == "p";
    return (object == "a" && (element == 3 || element == 4)) || (object == "b" && element == 7);
}

} // namespace

TEST(Selection, ObjectsKeepTheirPickOrderAndTheLastIsThePrimary)
{
    SelectionModel s;
    s.select({"b", "a", "b", "gone"}, {}, exists);
    EXPECT_EQ(s.objects(), (QStringList{"b", "a"}));
    EXPECT_EQ(s.primary(), "a");
    EXPECT_EQ(s.subject(), Subject::Overlay);
    EXPECT_EQ(s.subjectCount(), 2);
}

TEST(Selection, OneTailAloneIsATailAndItsBalloonOnlyCarriesIt)
{
    SelectionModel s;
    s.select({}, {{"a", 4}}, exists);
    EXPECT_EQ(s.subject(), Subject::Tail);
    EXPECT_EQ(s.single(), (ElementRef{"a", 4}));
    EXPECT_EQ(s.objects(), QStringList{"a"});   // on the canvas, for its handles
    EXPECT_EQ(s.carriers(), QStringList{"a"});  // but not a subject
    EXPECT_EQ(s.subjectCount(), 1);
}

TEST(Selection, ATailWithAnythingElseIsASet)
{
    SelectionModel s;
    s.select({"a"}, {{"a", 3}}, exists);   // its own balloon picked too: still the one tail, carried by nothing
    EXPECT_EQ(s.subject(), Subject::Tail);
    EXPECT_TRUE(s.carriers().isEmpty());

    s.select({"b"}, {{"a", 3}}, exists);   // another object
    EXPECT_EQ(s.subject(), Subject::Overlay);
    EXPECT_EQ(s.single(), ElementRef());
    EXPECT_EQ(s.subjectCount(), 2);

    s.select({}, {{"a", 3}, {"b", 7}}, exists);   // two tails
    EXPECT_EQ(s.subject(), Subject::Overlay);
    EXPECT_EQ(s.carriers(), (QStringList{"a", "b"}));
    EXPECT_EQ(s.subjectCount(), 2);
}

TEST(Selection, WhatIsGoneDropsOutAndTheRestStays)
{
    SelectionModel s;
    s.select({"p"}, {{"a", 3}, {"a", 9}, {"a", 3}, {"c", 1}}, exists);
    EXPECT_EQ(s.elements(), QList<ElementRef>{(ElementRef{"a", 3})});
    EXPECT_EQ(s.objects(), (QStringList{"p", "a"}));

    // Selecting what was picked again keeps it, the carrier still only a carrier.
    s.select(s.picked(), s.elements(), exists);
    EXPECT_EQ(s.elements(), QList<ElementRef>{(ElementRef{"a", 3})});
    EXPECT_EQ(s.objects(), (QStringList{"p", "a"}));
    EXPECT_EQ(s.carriers(), QStringList{"a"});
}

TEST(Selection, TheStripOrAPageIsOneOfAKind)
{
    SelectionModel s;
    s.select({"a"}, {{"b", 7}}, exists);
    s.selectPage("page-2");
    EXPECT_EQ(s.subject(), Subject::Page);
    EXPECT_EQ(s.page(), "page-2");
    EXPECT_TRUE(s.objects().isEmpty());
    EXPECT_TRUE(s.elements().isEmpty());

    s.selectStrip();
    EXPECT_EQ(s.subject(), Subject::Strip);
    EXPECT_TRUE(s.page().isEmpty());

    s.select({}, {}, exists);
    EXPECT_EQ(s.subject(), Subject::None);
}
