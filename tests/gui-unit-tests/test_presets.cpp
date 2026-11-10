// A preset is a look copied into an object, which remembers where it came from (PresetOrigin) as a hint:
// what applying one carries and leaves, and how a preset keeps the uid that hint names it by.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "presetstore.hpp"
#include "recordsvg.hpp"

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
ObjectRecord placed()
{
    ObjectRecord a;
    a.shape.kind = ObjectRecord::Shape::Box;
    a.box        = QSize(300, 160);
    a.text.body  = QStringLiteral("Hello");
    a.styleSeed  = 123456;
    Tail t;
    t.tip = QPointF(40, 220);
    a.tails.items.append(t);
    return a;
}

} // namespace

/**
 * \brief A preset is renamed in place, keeping its look; a built-in, an empty name and a name another of
 *        the artist's own has are refused.
 */
TEST_F(Presets, ARenameKeepsTheLookAndRefusesATakenName)
{
    PresetStore store;
    const int a = store.save(QStringLiteral("Rename test A"), placed(), /*replaceExisting=*/true);
    const int b = store.save(QStringLiteral("Rename test B"), placed(), /*replaceExisting=*/true);
    ASSERT_GE(a, store.builtinCount());
    ASSERT_GE(b, store.builtinCount());
    const ObjectRecord look = store.presets().at(a).record;

    EXPECT_FALSE(store.rename(a, QStringLiteral("rename test b")));   // taken, whatever the case
    EXPECT_FALSE(store.rename(a, QStringLiteral("   ")));
    EXPECT_FALSE(store.rename(0, QStringLiteral("Rename test C")));   // a built-in
    EXPECT_TRUE(store.rename(a, QStringLiteral("  Rename test C ")));
    EXPECT_EQ(store.presets().at(a).name, QStringLiteral("Rename test C"));
    EXPECT_EQ(store.presets().at(a).record, look);

    store.remove(qMax(a, b));   // the application's settings, shared with the next run
    store.remove(qMin(a, b));
}

TEST_F(Presets, APresetBringsItsPaddingAndLineSpacing)
{
    const PresetStore store;
    BubblePreset      p = store.presets().first();
    p.record.text.paddingSide = -4;
    p.record.text.paddingTop  = 12;
    p.record.text.lineSpacing = 90;

    const ObjectRecord look = PresetStore::applied(p, placed(), /*keepShape=*/true);
    EXPECT_EQ(look.text.paddingSide, -4);
    EXPECT_EQ(look.text.paddingTop, 12);
    EXPECT_EQ(look.text.lineSpacing, 90);
}

/**
 * \brief Applying a preset makes it the object's origin, by its uid and name; *No preset* (a preset with no
 *        uid) leaves none. Every built-in has a uid of its own.
 */
TEST_F(Presets, AnAppliedPresetBecomesTheObjectsOrigin)
{
    const PresetStore store;
    QStringList       uids;
    for (int i = 0; i < store.presets().size(); ++i) {
        const BubblePreset& p = store.presets().at(i);
        const ObjectRecord  a = PresetStore::applied(p, placed(), /*keepShape=*/false);
        EXPECT_EQ(a.presetOrigin.uid, p.uid) << p.name.toStdString();
        EXPECT_EQ(a.presetOrigin.name, p.name);
        EXPECT_FALSE(p.uid.isEmpty()) << p.name.toStdString();
        EXPECT_FALSE(uids.contains(p.uid)) << p.name.toStdString();
        uids << p.uid;
        EXPECT_EQ(store.indexOf(p.uid), i);
    }
    const ObjectRecord based = PresetStore::applied(store.presets().first(), placed(), false);
    EXPECT_TRUE(PresetStore::applied(BubblePreset{}, based, false).presetOrigin.isEmpty());
}

/**
 * \brief A preset keeps its uid through an edit saved over it, a rename, an export and an import, and
 *        stores no origin of its own.
 */
TEST_F(Presets, APresetKeepsItsUidAndStoresNoOrigin)
{
    PresetStore  store;
    ObjectRecord look = PresetStore::applied(store.presets().first(), placed(), false);
    ASSERT_FALSE(look.presetOrigin.isEmpty());

    const int     at  = store.save(QStringLiteral("Uid test"), look, /*replaceExisting=*/true);
    const QString uid = store.presets().at(at).uid;
    EXPECT_FALSE(uid.isEmpty());
    EXPECT_TRUE(store.presets().at(at).record.presetOrigin.isEmpty());

    look.skin.fill.colour = QColor(Qt::red);
    EXPECT_EQ(store.save(QStringLiteral("Uid test"), look, /*replaceExisting=*/true), at);
    EXPECT_EQ(store.presets().at(at).uid, uid);
    ASSERT_TRUE(store.rename(at, QStringLiteral("Uid test renamed")));
    EXPECT_EQ(store.presets().at(at).uid, uid);

    const QTemporaryDir dir;
    const QString       pack = QDir(dir.path()).filePath(QStringLiteral("pack.json"));
    QString             error;
    ASSERT_TRUE(store.exportPack(pack, &error)) << error.toStdString();
    store.remove(at);
    ASSERT_EQ(store.indexOf(uid), -1);
    ASSERT_GE(store.importPack(pack, &error), 1) << error.toStdString();
    EXPECT_GE(store.indexOf(uid), store.builtinCount());

    store.remove(store.indexOf(uid));   // the application's settings, shared with the next run
}

/**
 * \brief An object's origin is kept where the object is: in its undo snapshot (JSON) and in its SVG.
 */
TEST_F(Presets, TheOriginTravelsWithTheObject)
{
    ObjectRecord a = placed();
    a.presetOrigin = {QStringLiteral("uid-1"), QStringLiteral("Shout")};
    EXPECT_EQ(ObjectRecord::fromJson(a.toJson()).presetOrigin, a.presetOrigin);
    EXPECT_TRUE(ObjectRecord::fromJson(placed().toJson()).presetOrigin.isEmpty());

    // Built by hand rather than through Svg::write(), which measures lettering and needs a
    // QGuiApplication this target does not have: what is under test is that the reader takes the origin.
    const QByteArray svg = QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + Svg::k_pmNamespace
                         + "\" width=\"10\" height=\"10\"><g pm:shape=\"box\" pm:box=\"300,160\""
                           " pm:presetUid=\"uid-1\" pm:presetName=\"Shout\"/></svg>";
    bool               ok   = false;
    const ObjectRecord back = Svg::read(svg, &ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(back.presetOrigin, a.presetOrigin);
}

/**
 * \brief A preset's kind is read from its look: the built-ins are bubble presets, and one saved from a text
 *        object — no shape — is a text preset, which the default reset leaves a text object.
 */
TEST_F(Presets, APresetSavedFromTextIsATextPreset)
{
    PresetStore store;
    for (int i = 0; i < store.builtinCount(); ++i)
        EXPECT_FALSE(isTextPreset(store.presets().at(i))) << store.presets().at(i).name.toStdString();

    ObjectRecord text = placed();
    text.shape.kind   = ObjectRecord::Shape::None;
    const int at      = store.save(QStringLiteral("Kind test"), text, /*replaceExisting=*/true);
    EXPECT_TRUE(isTextPreset(store.presets().at(at)));
    // *Reset to default* on a text object: the default look, with its own no-shape kept.
    EXPECT_FALSE(PresetStore::applied(BubblePreset{}, text, /*keepShape=*/true).hasSilhouette());
    store.remove(at);   // the application's settings, shared with the next run
}
