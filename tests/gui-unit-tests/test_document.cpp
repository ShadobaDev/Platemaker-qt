// The strip editor's objects as the project holds them, and the operations on them: each says whether it
// changed anything. QtCore and the library's types only, so tested without a scene.

#include <gtest/gtest.h>

#include "objects/document.hpp"

using StripEdit::Document;
using Platemaker::Models::BlendMode;

namespace {

constexpr double k_tolerance = 1e-12;   //!< Fractions are copied, not computed.

//! Three overlays, back to front: "a", "b", "c", all on page "p1".
Document three()
{
    std::vector<Document::Overlay> list(3);
    const char* ids[] = {"a", "b", "c"};
    for (int i = 0; i < 3; ++i) {
        list[size_t(i)].uid            = ids[i];
        list[size_t(i)].anchorInputUid = "p1";
    }
    Document d;
    d.reset(list);
    return d;
}

QStringList order(const Document& d)
{
    QStringList out;
    for (const Document::Overlay& o : d.overlays())
        out << QString::fromStdString(o.uid);
    return out;
}

} // namespace

TEST(Document, TheStackOrderMovesOnePlaceAndNotPastItsEnds)
{
    Document d = three();
    EXPECT_TRUE(d.move("a", true));
    EXPECT_EQ(order(d), (QStringList{"b", "a", "c"}));
    EXPECT_FALSE(d.move("c", true)) << "already in front";
    EXPECT_FALSE(d.move("b", false)) << "already at the back";
    EXPECT_FALSE(d.move("gone", true));

    EXPECT_EQ(d.setOrder({"c", "a", "b"}), Document::Reorder::Done);
    EXPECT_EQ(order(d), (QStringList{"c", "a", "b"}));
    EXPECT_EQ(d.setOrder({"strip", "c", "a", "b"}), Document::Reorder::Unchanged) << "a row that is no overlay drops out";
    EXPECT_EQ(d.setOrder({"c", "a"}), Document::Reorder::Refused) << "an order that lost one";
    EXPECT_EQ(order(d), (QStringList{"c", "a", "b"}));
}

TEST(Document, BlendIsSharedOrMixedAndSetOnlyWhereItDiffers)
{
    Document d = three();
    EXPECT_EQ(d.blendOf({"a", "b"}), BlendMode::Over);
    EXPECT_EQ(d.setBlend({"a"}, BlendMode::Multiply), QStringList{"a"});
    EXPECT_FALSE(d.blendOf({"a", "b"}).has_value()) << "mixed";
    EXPECT_EQ(d.setBlend({"a", "b"}, BlendMode::Multiply), QStringList{"b"}) << "a has it already";
    EXPECT_FALSE(d.blendOf({}).has_value());
}

TEST(Document, AnOverlaysPlaceAndPropertiesChangeOnlyWhenTheyDiffer)
{
    Document d = three();
    EXPECT_FALSE(d.setEnabled("a", true)) << "shown already";
    EXPECT_TRUE(d.setEnabled("a", false));
    EXPECT_FALSE(d.overlay("a")->enabled);

    EXPECT_FALSE(d.setAnchor("b", "p1"));
    EXPECT_TRUE(d.setAnchor("b", "p2"));
    EXPECT_EQ(d.overlay("b")->anchorInputUid, "p2");

    EXPECT_TRUE(d.setWidth("c", 0.5));
    EXPECT_FALSE(d.setWidth("c", 0.5));

    ASSERT_TRUE(d.setPlace("c", {"p3", 0.1, 0.2, 0.3}));
    EXPECT_EQ(d.overlay("c")->anchorInputUid, "p3");
    EXPECT_NEAR(d.overlay("c")->xFrac, 0.1, k_tolerance);
    EXPECT_NEAR(d.overlay("c")->yFrac, 0.2, k_tolerance);
    EXPECT_NEAR(d.overlay("c")->wFrac, 0.3, k_tolerance);
    EXPECT_FALSE(d.setPlace("gone", {}));

    d.remove({"a", "c"});
    EXPECT_EQ(order(d), QStringList{"b"});
    EXPECT_EQ(d.overlay("a"), nullptr);
}
