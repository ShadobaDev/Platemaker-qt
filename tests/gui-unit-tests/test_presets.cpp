// A preset is a look copied into an object, which remembers where it came from (PresetOrigin) as a hint:
// what applying one carries and leaves, and how a preset keeps the uid that hint names it by.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "objectparts.hpp"
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
Composition placed()
{
    Composition a = balloonObject();
    shapeOf(a).kind = ShapeProperties::Kind::Box;
    boxOf(a)        = QSize(300, 160);
    textOf(a).body  = QStringLiteral("Hello");
    seedOf(a)  = 123456;
    Tail t;
    t.tip = QPointF(40, 220);
    setTails(a, {t});
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
    const Composition look = store.presets().at(a).look();

    EXPECT_FALSE(store.rename(a, QStringLiteral("rename test b")));   // taken, whatever the case
    EXPECT_FALSE(store.rename(a, QStringLiteral("   ")));
    EXPECT_FALSE(store.rename(0, QStringLiteral("Rename test C")));   // a built-in
    EXPECT_TRUE(store.rename(a, QStringLiteral("  Rename test C ")));
    EXPECT_EQ(store.presets().at(a).name, QStringLiteral("Rename test C"));
    EXPECT_EQ(store.presets().at(a).look(), look);

    store.remove(qMax(a, b));   // the application's settings, shared with the next run
    store.remove(qMin(a, b));
}

TEST_F(Presets, APresetBringsItsPaddingAndLineSpacing)
{
    const PresetStore store;
    BubblePreset      p = store.presets().first();
    p.lettering.paddingSide = -4;
    p.lettering.paddingTop  = 12;
    p.lettering.lineSpacing = 90;

    const Composition look = PresetStore::applied(p, placed(), /*keepShape=*/true);
    EXPECT_EQ(textOf(look).paddingSide, -4);
    EXPECT_EQ(textOf(look).paddingTop, 12);
    EXPECT_EQ(textOf(look).lineSpacing, 90);
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
        const Composition  a = PresetStore::applied(p, placed(), /*keepShape=*/false);
        EXPECT_EQ(originOf(a).uid, p.uid) << p.name.toStdString();
        EXPECT_EQ(originOf(a).name, p.name);
        EXPECT_FALSE(p.uid.isEmpty()) << p.name.toStdString();
        EXPECT_FALSE(uids.contains(p.uid)) << p.name.toStdString();
        uids << p.uid;
        EXPECT_EQ(store.indexOf(p.uid), i);
    }
    const Composition based = PresetStore::applied(store.presets().first(), placed(), false);
    EXPECT_TRUE(originOf(PresetStore::applied(BubblePreset{}, based, false)).isEmpty());
}

/**
 * \brief A preset keeps its uid through an edit saved over it, a rename, an export and an import, and
 *        stores no origin of its own.
 */
TEST_F(Presets, APresetKeepsItsUidAndStoresNoOrigin)
{
    PresetStore  store;
    Composition look = PresetStore::applied(store.presets().first(), placed(), false);
    ASSERT_FALSE(originOf(look).isEmpty());

    const int     at  = store.save(QStringLiteral("Uid test"), look, /*replaceExisting=*/true);
    const QString uid = store.presets().at(at).uid;
    EXPECT_FALSE(uid.isEmpty());
    EXPECT_TRUE(originOf(store.presets().at(at).look()).isEmpty());

    skinOf(look).fill.colour = QColor(Qt::red);
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
    // The look comes back as it was saved: a balloon's shape and style, and the lettering without its words.
    BubblePreset saved = BubblePreset::of(QStringLiteral("Uid test renamed"), look, uid);
    EXPECT_TRUE(store.presets().at(store.indexOf(uid)).look() == saved.look());
    EXPECT_TRUE(store.presets().at(store.indexOf(uid)).lettering.body.isEmpty());

    store.remove(store.indexOf(uid));   // the application's settings, shared with the next run
}

/**
 * \brief An object's origin is kept where the object is: in its content (JSON) and in its SVG's copy of it.
 */
TEST_F(Presets, TheOriginTravelsWithTheObject)
{
    Composition a = placed();
    originOf(a) = {QStringLiteral("uid-1"), QStringLiteral("Shout")};
    EXPECT_EQ(originOf(compositionFromJson(toJson(a))), originOf(a));
    EXPECT_TRUE(originOf(compositionFromJson(toJson(placed()))).isEmpty());

    // Built by hand rather than through Svg::write(), which measures lettering and needs a
    // QGuiApplication this target does not have: what is under test is that the reader takes the origin.
    const QString    content = QString::fromUtf8(QJsonDocument(toJson(a)).toJson(QJsonDocument::Compact));
    const QByteArray svg = QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + Svg::k_pmNamespace
                         + "\" width=\"10\" height=\"10\"><pm:content>" + content.toHtmlEscaped().toUtf8()
                         + "</pm:content></svg>";
    bool               ok   = false;
    const Composition back = Svg::read(svg, &ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(originOf(back), originOf(a));
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

    const Composition text = withoutBalloon(placed());
    const int at      = store.save(QStringLiteral("Kind test"), text, /*replaceExisting=*/true);
    EXPECT_TRUE(isTextPreset(store.presets().at(at)));
    // *Reset to default* on a text object: the default look, with its own no-shape kept.
    EXPECT_FALSE(PresetStore::applied(BubblePreset{}, text, /*keepShape=*/true).balloon.has_value());
    store.remove(at);   // the application's settings, shared with the next run
}
