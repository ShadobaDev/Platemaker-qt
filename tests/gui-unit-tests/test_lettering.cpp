// Lettering laid out in a font: its outlines (Geometry::textOutline()) and its fills (Geometry::textFills()).
// The font is the application's default, whatever the machine has, so each test asks what holds in any font.

#include <gtest/gtest.h>

#include "objectparts.hpp"
#include "geometry.hpp"

#include <QFontMetricsF>

namespace {

//! Two lines of one I each, the second a tenth of a line under the first: over it, so the block's middle
//! lies in both.
Composition overlappingIs()
{
    Composition a         = textObject(QSize(300, 300));
    textOf(a).body        = QStringLiteral("I\nI");
    textOf(a).pixelSize   = 60;
    textOf(a).lineSpacing = 10;
    return a;
}

} // namespace

TEST(Lettering, OverlappingLettersPaintOnce)
{
    const Composition  a       = overlappingIs();
    const QPainterPath letters = Geometry::textOutline(a, *wordsOf(a));
    ASSERT_FALSE(letters.isEmpty());
    EXPECT_TRUE(letters.contains(letters.boundingRect().center())) << "the overlap is cut out";
}

TEST(Lettering, OverlappingLettersInARunPaintOnce)
{
    // A run sets its letters fragment by fragment, and a run's fill gathers them apart: both must paint the
    // overlap once, as the words' own letters do.
    Composition a = overlappingIs();
    TextRun     run;
    run.from      = 0;
    run.length    = int(textOf(a).body.size());
    run.pixelSize = 61;
    run.fill      = Paint{QColor(200, 0, 0), QColor(255, 255, 255)};
    textOf(a).runs = {run};

    const QPainterPath letters = Geometry::textOutline(a, *wordsOf(a));
    ASSERT_FALSE(letters.isEmpty());
    EXPECT_TRUE(letters.contains(letters.boundingRect().center())) << "the overlap is cut out of the outline";

    const Geometry::TextFills fills = Geometry::textFills(a, *wordsOf(a));
    ASSERT_TRUE(fills.contains(0));
    const QPainterPath& filled = fills.value(0);
    EXPECT_TRUE(filled.contains(filled.boundingRect().center())) << "the overlap is cut out of the run's fill";
}

namespace {

//! One line of four letters, @p spacing apart (TextProperties::letterSpacing), in a box that holds it.
Composition fourLetters(int spacing)
{
    Composition a           = textObject(QSize(800, 200));
    textOf(a).body          = QStringLiteral("HHHH");
    textOf(a).pixelSize     = 50;
    textOf(a).letterSpacing = spacing;
    return a;
}

//! TextProperties::letterSpacing's unit: thousandths of an em.
constexpr qreal k_perEm   = 1000.0;
//! A tenth of an em, in it.
constexpr int   k_tenthEm = 100;
//! Widths measured from a font's layout: within a pixel of what the spacing says.
constexpr qreal k_layoutPx = 1.0;

} // namespace

TEST(Lettering, LetterSpacingOpensTheLetters)
{
    const Composition plain  = fourLetters(0);
    const Composition spaced = fourLetters(k_tenthEm);
    const qreal       step   = textOf(plain).pixelSize * k_tenthEm / k_perEm;   // in pixels

    // The letters: three gaps between four letters, each a step wider.
    const qreal drawn = Geometry::textOutline(spaced, *wordsOf(spaced)).boundingRect().width()
                      - Geometry::textOutline(plain, *wordsOf(plain)).boundingRect().width();
    EXPECT_NEAR(drawn, 3 * step, k_layoutPx);

    // The block Fit to text holds grows by the same: no step after the last letter.
    const qreal block = Geometry::textBlockSize(*wordsOf(spaced)).width() - Geometry::textBlockSize(*wordsOf(plain)).width();
    EXPECT_NEAR(block, 3 * step, k_layoutPx);

    // Centred, the letters stay centred: they open both ways.
    const auto middle = [](const Composition& c) { return Geometry::textOutline(c, *wordsOf(c)).boundingRect().center().x(); };
    EXPECT_NEAR(middle(spaced), middle(plain), k_layoutPx);
}

TEST(Lettering, ARunsLetterSpacingIsOfItsOwnSize)
{
    // The same tenth of an em, set by a run twice the words' size: twice the pixels.
    Composition a = fourLetters(0);
    TextRun     run;
    run.from          = 0;
    run.length        = 4;
    run.pixelSize     = 2 * textOf(a).pixelSize;
    run.letterSpacing = k_tenthEm;
    Composition plain = a;
    run.letterSpacing.reset();
    textOf(plain).runs = {run};
    run.letterSpacing = k_tenthEm;
    textOf(a).runs = {run};

    const qreal drawn = Geometry::textOutline(a, *wordsOf(a)).boundingRect().width()
                      - Geometry::textOutline(plain, *wordsOf(plain)).boundingRect().width();
    EXPECT_NEAR(drawn, 3 * *run.pixelSize * k_tenthEm / k_perEm, k_layoutPx);
}

namespace {

//! Three lines, @p spacing per cent of the font's own pitch apart (TextProperties::lineSpacing), centred.
Composition threeLines(int spacing)
{
    Composition a         = textObject(QSize(600, 600));
    textOf(a).body        = QStringLiteral("fghjk\nqwerty\nsdfgh");
    textOf(a).pixelSize   = 40;
    textOf(a).lineSpacing = spacing;
    return a;
}

//! TextProperties::lineSpacing's unit, and a spacing three tenths of a pitch over the font's own in it.
constexpr qreal k_perCent = 100.0;
constexpr int   k_wider   = 130;

} // namespace

TEST(Lettering, LineSpacingIsBetweenLinesOnly)
{
    const Composition plain  = threeLines(100);
    const Composition spaced = threeLines(k_wider);
    const qreal       pitch  = Geometry::textBlockSize(*wordsOf(plain)).height() / 3;
    const qreal       more   = pitch * (k_wider / k_perCent - 1.0);   // what each gap grows by

    // Two gaps between three lines, and none under the last.
    const qreal grew = Geometry::textBlockSize(*wordsOf(spaced)).height() - Geometry::textBlockSize(*wordsOf(plain)).height();
    EXPECT_NEAR(grew, 2 * more, k_layoutPx);

    // Centred, the letters stay centred: the lines open both ways from the middle one.
    const auto middle = [](const Composition& c) { return Geometry::textOutline(c, *wordsOf(c)).boundingRect().center().y(); };
    EXPECT_NEAR(middle(spaced), middle(plain), k_layoutPx);
}

TEST(Lettering, KerningOffSetsEachLetterByItsOwnAdvance)
{
    // Whatever the font kerns, with kerning off a line is its letters' advances end to end; on, what the font says
    // the pairs make. "AVATAR" has pairs most faces kern; a face that kerns none passes both.
    Composition a         = textObject(QSize(800, 200));
    textOf(a).body        = QStringLiteral("AVATAR");
    textOf(a).pixelSize   = 60;
    QFont font;
    font.setPixelSize(textOf(a).pixelSize);

    textOf(a).kerning = false;
    font.setKerning(false);
    qreal apart = 0.0;
    for (const QChar ch : textOf(a).body)
        apart += QFontMetricsF(font).horizontalAdvance(ch);
    EXPECT_NEAR(Geometry::textBlockSize(*wordsOf(a)).width(), apart, k_layoutPx);

    textOf(a).kerning = true;
    font.setKerning(true);
    EXPECT_NEAR(Geometry::textBlockSize(*wordsOf(a)).width(), QFontMetricsF(font).horizontalAdvance(textOf(a).body),
                k_layoutPx);
}

namespace {

//! Lettering set down in columns (TextProperties::vertical), @p pixelSize letters, in a box that holds it.
Composition columns(const QString& body, int pixelSize = 50)
{
    Composition a       = textObject(QSize(600, 600));
    textOf(a).body      = body;
    textOf(a).pixelSize = pixelSize;
    textOf(a).vertical  = true;
    return a;
}

//! The middle across of a run's letters (the run's fill, TextFills key 0) and of all the letters.
std::pair<qreal, qreal> runAndAll(const Composition& c)
{
    return {Geometry::textFills(c, *wordsOf(c)).value(0).boundingRect().center().x(),
            Geometry::textOutline(c, *wordsOf(c)).boundingRect().center().x()};
}

//! A run over @p length letters from @p from, with a fill of its own, so its letters can be told apart.
TextRun filledRun(int from, int length)
{
    TextRun run;
    run.from   = from;
    run.length = length;
    run.fill   = Paint{QColor(200, 0, 0), QColor(255, 255, 255)};
    return run;
}

} // namespace

TEST(Lettering, AColumnIsItsLettersEmsAndTheSpacingBetween)
{
    // Four letters and a space: five cells an em tall, the spacing between them, none after the last.
    Composition a           = columns(QStringLiteral("HH HH"));
    textOf(a).letterSpacing = k_tenthEm;
    const qreal em          = textOf(a).pixelSize;
    EXPECT_NEAR(Geometry::textBlockSize(*wordsOf(a)).height(), 5 * em + 4 * em * k_tenthEm / k_perEm, k_layoutPx);

    // A run's letters are cells of their own size.
    TextRun run;
    run.from      = 0;
    run.length    = 1;
    run.pixelSize = 2 * textOf(a).pixelSize;
    textOf(a).runs = {run};
    textOf(a).letterSpacing = 0;
    EXPECT_NEAR(Geometry::textBlockSize(*wordsOf(a)).height(), 6 * em, k_layoutPx);
}

TEST(Lettering, ColumnsAreALinesHeightThickAndLineSpacingApart)
{
    Composition a = columns(QStringLiteral("HH\nHH"));
    QFont       font;
    font.setPixelSize(textOf(a).pixelSize);
    const qreal thickness = QFontMetricsF(font).height();
    EXPECT_NEAR(Geometry::textBlockSize(*wordsOf(a)).width(), 2 * thickness, k_layoutPx);

    // One gap between two columns grows; nothing beyond them.
    textOf(a).lineSpacing = k_wider;
    EXPECT_NEAR(Geometry::textBlockSize(*wordsOf(a)).width(), 2 * thickness + thickness * (k_wider / k_perCent - 1.0),
                k_layoutPx);
}

TEST(Lettering, ColumnsFollowEachOtherRightOrLeft)
{
    // The first line's letters, a run's, are the first column: on the left, or with columns to the left on the right.
    Composition a  = columns(QStringLiteral("HH\nHH"));
    textOf(a).runs = {filledRun(0, 2)};
    const auto [right, rightAll] = runAndAll(a);
    EXPECT_LT(right, rightAll);
    textOf(a).columnsLeft      = true;
    const auto [left, leftAll] = runAndAll(a);
    EXPECT_GT(left, leftAll);
    EXPECT_NEAR(left - leftAll, rightAll - right, k_layoutPx) << "the columns mirror each other";
}

TEST(Lettering, ValignPlacesEachColumnInTheBlock)
{
    // A short column beside a long one: its letters at the top of the block, or at its foot, as the long one's are.
    Composition a  = columns(QStringLiteral("H\nHHHH"));
    textOf(a).runs = {filledRun(0, 1)};
    const auto ends = [](const Composition& c) {
        const QRectF shortOne = Geometry::textFills(c, *wordsOf(c)).value(0).boundingRect();
        const QRectF all      = Geometry::textOutline(c, *wordsOf(c)).boundingRect();
        return std::pair{shortOne, all};
    };
    textOf(a).valign = Qt::AlignTop;
    const auto [top, topAll] = ends(a);
    EXPECT_NEAR(top.top(), topAll.top(), k_layoutPx);
    textOf(a).valign = Qt::AlignBottom;
    const auto [foot, footAll] = ends(a);
    EXPECT_NEAR(foot.bottom(), footAll.bottom(), k_layoutPx);
}

TEST(Lettering, FitToTextHoldsAColumn)
{
    Composition a = columns(QStringLiteral("HHHHHHHH\nHH"));
    resize(a, QSize(100, 100));
    ASSERT_TRUE(Geometry::textOverflows(a, *wordsOf(a)));
    resize(a, Geometry::fittedBox(a));
    EXPECT_FALSE(Geometry::textOverflows(a, *wordsOf(a)));
    EXPECT_GT(sizeOf(a).height(), sizeOf(a).width()) << "a tall column, two columns wide";
}
