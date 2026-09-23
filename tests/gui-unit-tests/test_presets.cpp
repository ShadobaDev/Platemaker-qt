// The preset badge is computed rather than stored, which is only honest if the comparison behind it is.
// These are the cases that decide what the artist sees: a look that still matches, a look that no longer
// does, and the three things a preset never carried in the first place.
#include <gtest/gtest.h>

#include <QCoreApplication>

#include "presetstore.hpp"

using namespace StripEdit;

namespace {

//! A store reading a settings file of its own, so a developer's own saved presets cannot decide a test.
class Presets : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QCoreApplication::setOrganizationName(QStringLiteral("Platemaker"));
        QCoreApplication::setApplicationName(QStringLiteral("platemaker-gui-tests"));
    }
};

//! A plain balloon with something in it, standing in for one the artist has placed.
Artifact placed()
{
    Artifact a;
    a.shape.kind = Artifact::Shape::Speech;
    a.box        = QSize(300, 160);
    a.text.body  = QStringLiteral("Hello");
    a.styleSeed  = 123456;
    Tail t;
    t.tip = QPointF(40, 220);
    a.tails.items.append(t);
    return a;
}

} // namespace

TEST_F(Presets, AnAppliedPresetIsRecognisedAsItself)
{
    const PresetStore store;
    ASSERT_FALSE(store.presets().isEmpty());

    for (int i = 0; i < store.presets().size(); ++i) {
        const Artifact look = PresetStore::applied(store.presets().at(i), placed(), false);
        // The first match wins, and two presets may legitimately describe the same look, so the test is
        // that the *look* is recognised — not that this exact index comes back.
        const int found = store.matching(look);
        ASSERT_GE(found, 0) << "preset " << i << " is not recognised after being applied";
        EXPECT_EQ(store.lookLabel(look), store.presets().at(found).name);
    }
}

TEST_F(Presets, OneChangedPropertyIsCustom)
{
    const PresetStore store;
    Artifact      look = PresetStore::applied(store.presets().first(), placed(), false);

    look.skin.fill = look.skin.fill == QColor(Qt::red) ? QColor(Qt::blue) : QColor(Qt::red);

    EXPECT_EQ(store.matching(look), -1);
    EXPECT_EQ(store.lookLabel(look), QStringLiteral("Custom"));
}

TEST_F(Presets, WhatAPresetNeverCarriedCannotBreakTheMatch)
{
    const PresetStore store;
    Artifact      look = PresetStore::applied(store.presets().first(), placed(), false);
    const int         was  = store.matching(look);
    ASSERT_GE(was, 0);

    // The lettering, the box and the tails are the object's own — a preset copies them across rather
    // than carrying them, so editing any of them leaves the look alone.
    look.text.body = QStringLiteral("something else entirely");
    look.box       = QSize(900, 40);
    look.tails.items.clear();
    EXPECT_EQ(store.matching(look), was);

    // And the style seed belongs to no group at all. Comparing whole artifacts would have made a
    // re-rolled outline report as a different look, which is the trap this comparison exists to avoid.
    look.styleSeed = 987654321;
    EXPECT_EQ(store.matching(look), was);
}

TEST_F(Presets, AnObjectWithNoShapeMatchesNoBalloonPreset)
{
    const PresetStore store;
    Artifact      a = placed();
    a.shape.kind        = Artifact::Shape::None;
    a.tails.items.clear();

    // Lettering with no balloon cannot be wearing a speech balloon's look, whatever its colours say.
    const int found = store.matching(a);
    if (found >= 0) {
        EXPECT_EQ(store.presets().at(found).artifact.shape.kind, Artifact::Shape::None);
    }
}
