#include "editor.h"
#include "ui_editor.h"
#include "flowlayout.h"
#include "advisorybar.h"
#include "colouradjustment.h"
#include "colourpair.h"
#include "cursors.h"
#include "gradepanel.h"
#include "object.h"
#include "objectcontroller.h"
#include "pagesource.h"
#include "objectstatepanel.h"
#include "presetstore.h"
#include "shapeeditor.h"
#include "stripstatepanel.h"
#include "artworkoptionspanel.h"
#include "assetstatepanel.h"
#include "tooloptionspanel.h"

#include <platemaker/models/colour_correction.hpp>

#include <algorithm>

#include <QFileInfo>
#include <QButtonGroup>
#include <QDebug>
#include <QEnterEvent>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyle>
#include <QStackedWidget>
#include <QStyleOptionGraphicsItem>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace StripEdit {

namespace {

/**
 * @brief Puts @p page into @p host inside a scroll area, and hands the area back to be added as the page.
 *
 * **A panel may not decide how wide its column is.** A stacked widget's minimum is its pages' minimum,
 * and a splitter may never take a child below that — so the right column grew the moment something was
 * selected and the properties appeared (measured: 18 points empty, 174 with one object's controls, and
 * more with the real panel). Every row in the list then slid sideways, out from under the pointer that
 * had just come down on a mute checkbox, and the click landed on the row instead. Inside a scroll area
 * the column's minimum is the scroll area's own — constant — and a panel too big for the column scrolls
 * rather than shoving it. That is also what lets the object list keep its third of the height when the
 * properties are long.
 */
[[nodiscard]] QScrollArea* scrolled(QWidget* page, QStackedWidget* host)
{
    auto* area = new QScrollArea(host);
    area->setFrameShape(QFrame::NoFrame);   // the panel already sits in a framed column
    area->setWidgetResizable(true);
    area->setWidget(page);
    return area;
}

/**
 * @brief How wide the **right** column starts, and the least it can be dragged to.
 *
 * A number rather than a question, and the difference between this column and the tool column is the
 * whole reason: the properties panel builds its controls when something is *selected*, so at
 * construction it has none and answers **55** — while the width has to be settled before anything is
 * selected, and asking again afterwards would be the column widening under the pointer, which is the
 * bug the scroll areas exist to stop. So it is measured once, by hand: with an object in it the panel
 * asks for 324 points and stops needing a horizontal scroll bar at 332, and 340 leaves room for the
 * vertical one.
 *
 * It is enforced as a *minimum* and not only as a starting size, because a size is outvoted by whatever
 * the artist last had saved — someone whose saved layout predates this would otherwise go on reading
 * half a spin box.
 */
constexpr int k_rightColumnPx = 340;

/**
 * @brief How wide a column has to be to hold @p panel whole.
 *
 * The tool options **can** be asked, where the properties cannot: they describe the next object rather
 * than a selected one, so every control exists from the moment the panel is built and the answer is
 * complete and final. Asking beats a constant here because it follows the screen — the same panel that
 * wants 339 points at one font and scale wants more at another, and a number measured on the author's
 * machine is a number that clips on somebody else's.
 */
[[nodiscard]] int columnWidthFor(const QWidget* panel)
{
    return panel->minimumSizeHint().width()
         + panel->style()->pixelMetric(QStyle::PM_ScrollBarExtent);   // the bar it will want first
}

//! How wide a splitter's grab area is. Wider than the default 1 point, which was as hard to hit as it
//! sounds.
constexpr int k_splitterHandlePx = 6;

//! The least the tool's options may be squeezed to. Below this the panel is a heading with no controls
//! under it, which says less than nothing about what the armed tool will do.
constexpr int k_toolOptionsFloorPx = 140;

/**
 * @brief Makes @p splitter's handles visible, in the palette's own colours.
 *
 * Neither the native style nor Fusion paints anything on a splitter handle (checked, both), so widening
 * one only widens the gap. Filling it does say where the seam is — and which of the two neighbouring
 * greys reads as a line depends on the theme, so it is chosen the same way `widgets/badge/` chooses a
 * chip's lightness: against the window's own.
 */
void showHandles(QSplitter* splitter)
{
    splitter->setHandleWidth(k_splitterHandlePx);
    const bool darkUi = splitter->palette().color(QPalette::Window).lightnessF() < 0.5;
    for (int i = 1; i < splitter->count(); ++i) {
        if (QSplitterHandle* handle = splitter->handle(i)) {
            handle->setAutoFillBackground(true);
            handle->setBackgroundRole(darkUi ? QPalette::Midlight : QPalette::Mid);
        }
    }
}

//! Pages built beyond the viewport on each side. One page is several slices tall, so ±1 already covers
//! a comfortable scroll ahead; a larger margin would multiply a much heavier unit of work.
constexpr int k_prefetchPages = 1;

/**
 * @brief One graphics item that draws every input page as its own image — seam-free.
 *
 * One QGraphicsPixmapItem per page leaves a 1px hairline at each join (QGraphicsView clips and rounds
 * each item's edge independently). Drawing all pages through one item, in one painter pass, tiles them
 * edge-to-edge with no seam at any zoom. The item is a thin view over the Editor: it owns no
 * pixels — it asks the viewer for each page's built pixmap (if ready) or its blurry proxy, so the
 * lazy/async machinery lives in one place.
 */
class StripItem : public QGraphicsItem
{
public:
    explicit StripItem(Editor *owner) : m_owner(owner) {}

    QRectF boundingRect() const override
    {
        const QSize s = m_owner->stripSize();
        return QRectF(0, 0, s.width(), s.height());
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        // Smooth the pixmap interior, but turn OFF edge antialiasing: with AA on, each drawPixmap
        // coverage-antialiases the destination rect's edges at fractional zoom, so the boundary row
        // between two pages is only partially covered and the background hairlines through — that is
        // the "1px frame". AA off makes adjacent pages tile with hard edges, each device row owned by
        // exactly one page, no bleed. (Local to this item — the view keeps AA for text/seam lines.)
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF exposed = option->exposedRect;
        for (int i = 0; i < m_owner->pageCount(); ++i) {
            const QRectF r = m_owner->pageRect(i);
            if (!r.intersects(exposed))
                continue;

            if (m_owner->gradeActive()) {
                const QPixmap graded = m_owner->gradedOf(i);
                if (!graded.isNull()) {
                    painter->drawPixmap(r.topLeft(), graded); // live grade preview
                    continue;
                }
                // not graded yet → briefly show the ungraded page below while the grade is produced
            }

            const QPixmap page = m_owner->pageOf(i);
            if (!page.isNull()) {
                painter->drawPixmap(r.topLeft(), page);     // sharp, at the strip's native scale
                continue;
            }
            const QPixmap proxy = m_owner->proxyOf(i);
            if (!proxy.isNull())
                painter->drawPixmap(r, proxy, QRectF(proxy.rect())); // blurry proxy, scaled into the page rect
            else
                painter->fillRect(r, m_owner->palette().color(QPalette::Base)); // brief neutral placeholder
        }
    }

private:
    Editor *m_owner;
};

} // namespace

Editor::Editor(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Editor)
{
    // Page memory: the feed, the proxy/sharp tiers and the graded previews. It reads m_layout, which
    // this widget owns and rebuilds, so it takes it by reference.
    m_pages = new PageSource(m_layout, this);
    connect(m_pages, &PageSource::pageReady, this, [this](int index) {
        if (m_item)
            m_item->update(pageRect(index));
    });

    // Toolbar buttons + the graphics view come from the Designer form; the runtime wiring is here.
    ui->setupUi(this);
    m_view      = ui->graphicsView;
    m_zoomLabel = ui->labelZoom;

    m_scene = new QGraphicsScene(this);
    m_view->setScene(m_scene);
    // Hand-drag to pan the big canvas; scrollbars + wheel cover the rest. No hardcoded background —
    // the strip sits on the themed palette (see the "inherit, don't hardcode colours" rule).
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->viewport()->installEventFilter(this);   // Ctrl+wheel zoom, and the pointer's own answer
    // Without this a widget hears about the mouse only while a button is held, which is what made the
    // cursor look as though it followed the last *click*: it could only be re-decided on release. The
    // cursor is a promise about what a press would do here, so it has to be re-decided while hovering.
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->setAcceptDrops(true);   // pictures, from ④'s preview or a file manager
    // Build the pages that scroll into view (plus a prefetch margin).
    connect(m_view->verticalScrollBar(),   &QScrollBar::valueChanged, this, &Editor::updateVisiblePages);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &Editor::updateVisiblePages);

    connect(ui->buttonZoomOut,   &QToolButton::clicked, this, &Editor::zoomOut);
    connect(ui->buttonZoomIn,    &QToolButton::clicked, this, &Editor::zoomIn);
    connect(ui->buttonFitWidth,  &QToolButton::clicked, this, &Editor::fitWidth);
    connect(ui->buttonZoomReset, &QToolButton::clicked, this, &Editor::resetZoom);
    connect(ui->buttonSeams, &QToolButton::toggled, this, [this](bool on) {
        for (QGraphicsLineItem *seam : std::as_const(m_seamItems))
            seam->setVisible(on);
    });
    connect(ui->buttonRenderView, &QToolButton::clicked, this, [this] { emit renderAndViewRequested(); });

    // --- Editor shell: the splitters, canvas, tool-options stack and artifact list come from the .ui
    // (editorBody = toolbox | canvas | rightPanel). Here we only fill the toolbox with a flowing grid of
    // square tool tiles (a flow layout can't be expressed in a .ui), give the stack a page per tool, and
    // set the splitter sizing (not a .ui property). Pan (the default) keeps today's behaviour: hand-drag
    // pan and no side panel.
    {
        // The rail is two rows, not one flow: the tiles, and under them the colour pair. They used to
        // share the flow, which worked and read badly — the pair took its turn in the grid as though it
        // were a ninth tool, and it is furniture.
        auto* railRows = new QVBoxLayout(ui->toolRail);
        railRows->setContentsMargins(0, 0, 0, 0);
        railRows->setSpacing(0);
        m_toolTiles   = new QWidget(ui->toolRail);
        auto* railLay = new FlowLayout(m_toolTiles, 6, 4, 4); // margin, hSpacing, vSpacing — wraps to fit
        railRows->addWidget(m_toolTiles);
        m_toolTiles->installEventFilter(this);   // see eventFilter: the tiles keep their own minimum
        m_toolGroup = new QButtonGroup(this);
        m_toolGroup->setExclusive(true);

        // The tool-options pages, under the rail — the tool's own settings, in the place every drawing
        // application puts them. One widget per *page*, not per tool: tools that author the same object
        // name the same page, so there is one set of controls and no chance of two drifting apart. A
        // tool with no options gets the hint page, which is never blank.
        // **A tool with no options is not a tool with nothing to say.** The page used to be blank, and a
        // blank panel under an armed tool reads as *nothing is armed* — which is how the bucket gets
        // picked by accident, and the next click paints. It carries the tool's name and its one
        // sentence, both from the registry row, so a new tool cannot arrive without them.
        auto* hintPage = new QWidget(ui->toolOptions);
        auto* hintLay  = new QVBoxLayout(hintPage);
        m_toolTitle    = new QLabel(hintPage);
        QFont titleFont = m_toolTitle->font();
        titleFont.setBold(true);
        m_toolTitle->setFont(titleFont);
        m_toolHint = new QLabel(hintPage);
        m_toolHint->setWordWrap(true);
        m_toolHint->setForegroundRole(QPalette::PlaceholderText);   // a remark, not an instruction
        hintLay->addWidget(m_toolTitle);
        hintLay->addWidget(m_toolHint);
        hintLay->addStretch(1);
        QHash<QString, int> pageIndex;
        pageIndex.insert(QString(), ui->toolOptions->addWidget(scrolled(hintPage, ui->toolOptions)));
        // The Grade tool's options are an image editor's colour menu: which adjustment, and its controls, applied to the
        // selected object. What a selected strip's grade *is* is shown on the right, in its state.
        m_gradePanel = new GradePanel(ui->toolOptions);
        pageIndex.insert(QStringLiteral("grade"),
                         ui->toolOptions->addWidget(scrolled(m_gradePanel, ui->toolOptions)));
        connect(m_gradePanel, &GradePanel::changed, this, [this](const Platemaker::Models::ColourCorrection& cc) {
            // Live edit: apply it, but do NOT push it back into the panel — the panel is the source
            // here, and re-syncing its widgets mid-drag would fight the slider the user is holding.
            applyGrade(cc);
        });
        connect(m_gradePanel, &GradePanel::committed, this,
                [this](const Platemaker::Models::ColourCorrection& cc, const QString& undoText) {
            emit colourCorrectionEdited(cc, undoText);   // settled: the owner persists it, one undo step
        });
        // The panel can only act on the strip; asked for it, it gets it.
        connect(m_gradePanel, &GradePanel::selectStripRequested, this, [this] {
            if (m_objects)
                m_objects->selectStrip();
        });
        // One panel for every tool that authors a `TextArtifact` — Bubble, Text, Caption — because they
        // author the same object and differ only in the shape they place, which each tool's row says.
        m_presets     = new PresetStore(this);
        m_toolOptions = new ToolOptionsPanel(*m_presets, ui->toolOptions);
        pageIndex.insert(QStringLiteral("artifact"),
                         ui->toolOptions->addWidget(scrolled(m_toolOptions, ui->toolOptions)));

        // The other create tool's options: which picture the next placement puts down. A separate panel
        // rather than a section of the one above, because they describe different kinds of object — the
        // arrangement §26 exists to keep straight.
        m_artworkOptions = new ArtworkOptionsPanel(ui->toolOptions);
        pageIndex.insert(QStringLiteral("artwork"),
                         ui->toolOptions->addWidget(scrolled(m_artworkOptions, ui->toolOptions)));
        connect(m_artworkOptions, &ArtworkOptionsPanel::artworkChanged, this, [this](const QString& f) {
            if (m_objects && m_tool == QLatin1String("artwork"))
                m_objects->setPlacementArtwork(f);
        });

        // The rail, built from the registry: a button per row, in the table's order, its id that row's
        // index. A row with no icon file draws its own — see refreshGeneratedToolIcons().
        for (int i = 0; i < tools().size(); ++i) {
            const Tool& t = tools().at(i);
            auto* b = new QToolButton(m_toolTiles);
            if (!t.icon.isEmpty())
                b->setIcon(QIcon(t.icon));
            b->setIconSize(QSize(26, 26));
            b->setToolTip(toolTooltip(t));   // the name, and the same sentence ④ shows
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setFixedSize(40, 40);       // square tile
            railLay->addWidget(b);
            m_toolGroup->addButton(b, i);
            m_toolPage.insert(t.id, pageIndex.value(t.page));
        }

        // The colour pair is **furniture**, not a tool: it sits under the tiles and stays there whichever
        // tool is active, because the tools that use it — the eyedropper fills it, an applicator spends
        // it — hold a reference to it rather than a colour of their own.
        m_colours       = new ColourPair(ui->toolRail);
        auto* colourRow = new QHBoxLayout;
        colourRow->setContentsMargins(6, 2, 6, 6);
        colourRow->addWidget(m_colours);
        colourRow->addStretch(1);       // left, where the tiles start
        railRows->addLayout(colourRow);
        railRows->addStretch(1);        // both rows hug the top; the rest of the rail is empty space

        refreshGeneratedToolIcons();   // the rows that carry no icon file draw their own

        // X and D over the canvas, as every drawing application binds them. Scoped to the view so that
        // typing an x into a balloon stays typing an x.
        const auto onCanvas = [this](QKeySequence key, void (ColourPair::*op)()) {
            auto* s = new QShortcut(key, m_view);
            s->setContext(Qt::WidgetWithChildrenShortcut);
            connect(s, &QShortcut::activated, m_colours, op);
        };
        onCanvas(QKeySequence(Qt::Key_X), &ColourPair::swap);
        onCanvas(QKeySequence(Qt::Key_D), &ColourPair::resetToDefaults);

        // The other question, and a different class for it: what the *selected* object is. The two used
        // to be one class sitting in two places, which is how they came to look like the same panel
        // twice. They now differ in what they contain, not only in what they mean.
        m_objectState = new ObjectStatePanel(*m_presets, ui->objectProperties);
        m_objectPage  = scrolled(m_objectState, ui->objectProperties);
        ui->objectProperties->addWidget(m_objectPage);

        // The strip and its pages are selected too, and they are not overlays — so they get the same
        // surface with different contents rather than an object panel full of sections that never apply.
        m_stripState = new StripStatePanel(ui->objectProperties);
        m_stripPage  = scrolled(m_stripState, ui->objectProperties);
        ui->objectProperties->addWidget(m_stripPage);

        // And the third kind. Artwork has no property groups — somebody else drew it — but it does
        // have a size, and a panel that says "nothing to edit" while the artist wants it half as big
        // is a panel that has stopped answering the question.
        m_assetState = new AssetStatePanel(ui->objectProperties);
        m_assetPage  = scrolled(m_assetState, ui->objectProperties);
        ui->objectProperties->addWidget(m_assetPage);
        connect(m_assetState, &AssetStatePanel::scaleChanged, this, [this](double percent) {
            if (m_objects)
                m_objects->scaleSelectedArtwork(percent, /*commit=*/false);   // live, no history
        });
        connect(m_assetState, &AssetStatePanel::scaleCommitted, this, [this](double percent) {
            if (m_objects)
                m_objects->scaleSelectedArtwork(percent, /*commit=*/true);
        });
        connect(m_assetState, &AssetStatePanel::deleteRequested, this, [this] {
            if (m_objects)
                m_objects->deleteSelectedOverlay();
        });
        connect(m_assetState, &AssetStatePanel::changed, this, [this](const TextArtifact& r) {
            if (m_objects)
                m_objects->applyArtworkRecord(r, /*commit=*/false);
        });
        connect(m_assetState, &AssetStatePanel::committed, this, [this](const TextArtifact& r) {
            if (m_objects)
                m_objects->applyArtworkRecord(r, /*commit=*/true);
        });
        connect(m_assetState, &AssetStatePanel::blendPicked, this,
                [this](Platemaker::Models::BlendMode b) {
                    if (m_objects)
                        m_objects->setSelectionBlend(b);
                });
        connect(m_stripState, &StripStatePanel::excludedToggled, this,
                [this](const QString& inputUid, bool excluded) {
            auto cc = m_cc;
            auto& skipped = cc.excludedInputUids;
            const std::string uid = inputUid.toStdString();
            const auto it = std::find(skipped.begin(), skipped.end(), uid);
            if (excluded == (it != skipped.end()))
                return;
            if (excluded)
                skipped.push_back(uid);
            else
                skipped.erase(it);
            // The owner persists it as one undo step and feeds it back, which is what updates the preview,
            // the page's row and this panel — the same round trip every grade edit takes.
            emit colourCorrectionEdited(cc, excluded ? tr("Exclude page from colour correction")
                                                     : tr("Include page in colour correction"));
        });
        // An adjustment listed on the strip: reopened in the Grade tool, or taken off the strip.
        connect(m_stripState, &StripStatePanel::adjustmentEditRequested, this, [this](ColourAdjustment a) {
            setTool(QStringLiteral("grade"));
            m_gradePanel->openAdjustment(a);
        });
        connect(m_stripState, &StripStatePanel::adjustmentRemoveRequested, this, [this](ColourAdjustment a) {
            emit colourCorrectionEdited(withoutColourAdjustment(m_cc, a),
                                        tr("Remove %1").arg(colourAdjustmentName(a)));
        });

        // Everything placed on the strip. It drives the scene, the list and the panel; it owns no
        // persistence, so every edit leaves through one of its four signals and comes back as a re-feed.
        m_objects = new ObjectController(m_scene, m_view, ui->artifactList, m_objectState,
                                         m_toolOptions, *m_presets, m_layout, this, this);
        // The object's menu spends the same pair the bucket does — it reads it, never writes it.
        m_objects->setColourSource(m_colours);
        connect(m_objects, &ObjectController::artifactCreated,        this, &Editor::artifactCreated);
        connect(m_objects, &ObjectController::overlaysEdited,         this, &Editor::overlaysEdited);
        connect(m_objects, &ObjectController::artworkImportRequested, this, &Editor::artworkImportRequested);
        connect(m_objects, &ObjectController::subjectChanged,         this, [this] { showSubject(); });
        connect(m_objects, &ObjectController::noted,                  this, &Editor::noted);
        // Splitter behaviour (not expressible in the .ui): canvas absorbs resize, panels keep their width.
        ui->editorBody->setStretchFactor(0, 0);   // toolbox
        ui->editorBody->setStretchFactor(1, 1);   // canvas
        ui->editorBody->setStretchFactor(2, 0);   // right panel
        // Neither side column can be dragged under what its panel needs. The tool column knows that
        // because its panel is complete; the right column is told, because its panel is not yet.
        const int toolW = qMax(k_rightColumnPx, columnWidthFor(m_toolOptions));
        ui->toolColumn->setMinimumWidth(toolW);
        ui->rightPanel->setMinimumWidth(k_rightColumnPx);
        ui->editorBody->setChildrenCollapsible(false);   // no column can be dragged out of existence
        ui->editorBody->setSizes({toolW, 700, k_rightColumnPx});
        ui->toolColumn->setStretchFactor(0, 0);    // the tile rail takes what it needs
        ui->toolColumn->setStretchFactor(1, 1);    // the options absorb the rest
        // The tools are never negotiable — the rail's minimum follows its own wrapping (see
        // eventFilter) — and the options keep a floor of their own, so neither can be shut by a drag.
        ui->toolOptions->setMinimumHeight(k_toolOptionsFloorPx);
        ui->toolColumn->setSizes({120, 600});
        ui->rightPanel->setStretchFactor(0, 2);    // object properties
        ui->rightPanel->setStretchFactor(1, 1);    // the object list
        // A third of the column, and stated rather than inferred: without sizes a splitter falls back to
        // its children's size hints, and the list's hint is one row tall — which is how it ended up a
        // sliver. QSplitter reads these as proportions, so the ratio is what survives, not the numbers.
        ui->rightPanel->setSizes({2, 1});
        restoreSplitterState();                    // ...unless the artist has already moved them
        for (QSplitter* s : {ui->editorBody, ui->toolColumn, ui->rightPanel})
            showHandles(s);

        connect(m_toolGroup, &QButtonGroup::idClicked, this,
                [this](int id) { setTool(tools().at(id).id); });

        setTool(QStringLiteral("select"));   // the default state: select and move, no tool armed
    }

    showEmptyState();
}

void Editor::setTool(const QString& id)
{
    const Tool* tool = toolById(id);
    if (!tool)
        return;
    m_tool = tool->id;
    if (auto* b = m_toolGroup->button(toolIndex(m_tool)))
        b->setChecked(true);
    ui->toolOptions->setCurrentIndex(m_toolPage.value(m_tool));
    // Filled whichever page is showing: the hint page is the one that displays it, and writing it
    // unconditionally means there is no state to get wrong when tools are switched quickly.
    if (m_toolTitle)
        m_toolTitle->setText(toolName(*tool));
    if (m_toolHint)
        m_toolHint->setText(toolHint(*tool));

    // What a left-drag on the **bare strip** does — the view's own business, and one property, which is
    // why Select and Pan are two tools. Both modes only act on a press no item took, so dragging an
    // object still moves it under either.
    //
    // **Order matters and must stay this way.** Leaving `ScrollHandDrag` makes the view unset the
    // viewport cursor, and entering it makes the view write an open hand; deciding our cursor after that
    // is what keeps the last word. Reverse these two lines and the pointer starts flickering again.
    m_view->setDragMode(tool->kind == ToolKind::Pan      ? QGraphicsView::ScrollHandDrag
                        : tool->kind == ToolKind::Select ? QGraphicsView::RubberBandDrag
                                                         : QGraphicsView::NoDrag);
    updateCursor();

    // Only the *tool's own options* follow the tool: a tool that places one shape says so, and the
    // Bubble tool leaves the choice on the tiles. This replaced two gates that each existed to say
    // "this tool makes a shapeless object" — one here, one in the object controller.
    //
    // The object-state panel deliberately gets no such call. It describes whatever is **selected**, and
    // a tool chosen minutes ago must not decide what a bubble's properties look like — selecting a
    // balloon under the Text tool used to show nothing but its lettering, an effect with no visible
    // cause. Which groups that panel shows comes from the selected object's own kind instead.
    if (m_toolOptions)
        m_toolOptions->setToolShape(tool->kind == ToolKind::Create ? tool->shape : std::nullopt);

    // What a placement puts down: a picture, or — when this is empty — a balloon. The second and last
    // thing the controller is told about the active tool, and told at the moment it is armed.
    if (m_objects) {
        QString artwork;
        if (m_tool == QLatin1String("artwork") && m_artworkOptions) {
            // Arming with nothing chosen asks once, here: before any drag, so a file dialog never lands
            // in the middle of one. A cancelled dialog arms nothing, and ④ says as much.
            if (m_artworkOptions->artwork().isEmpty())
                m_artworkOptions->chooseArtwork();
            artwork = m_artworkOptions->artwork();
        }
        m_objects->setPlacementArtwork(artwork);
    }

    // The right column stays put under every tool, and **live** under every tool. It was briefly
    // hidden for Pan and Grade, which resized the canvas and made the strip jump sideways; then it was
    // merely greyed, on the grounds that its highlight would otherwise drift from a canvas whose items
    // were not selectable. Both were treating a symptom — the items are selectable now, so the list has
    // a selection to agree with and needs no gate at all.

    // An image editor always has an active layer for a colour tool to act on; here the strip is that
    // layer. Picking the Grade tool with nothing selected selects it, rather than opening on a panel
    // that can only say "select something first". A selection the artist made is left alone — the panel
    // explains instead.
    if (tool->kind == ToolKind::Grade && m_objects->subject() == ObjectController::Subject::None)
        m_objects->selectStrip();
}


void Editor::applyGrade(const Platemaker::Models::ColourCorrection& cc)
{
    m_cc = cc;
    if (m_objects) {
        QSet<QString> skipped;
        for (const auto& uid : cc.excludedInputUids)
            skipped.insert(QString::fromStdString(uid));
        m_objects->setExcludedPages(skipped);
    }
    showSubject();   // a selected strip or page describes the grade, so it follows it
    if (m_pages->setColourCorrection(cc))
        refreshGradePreview();
}

void Editor::showSubject()
{
    if (!m_objects || !m_stripState || !m_objectState)
        return;

    // The Grade tool acts on the selection, so it is told what that is whether or not it is showing.
    if (m_gradePanel) {
        const auto s = m_objects->subject();
        m_gradePanel->setTarget(s == ObjectController::Subject::Strip ? GradePanel::Target::Strip
                                : s == ObjectController::Subject::Page ? GradePanel::Target::Page
                                                                         : GradePanel::Target::Other);
    }

    const auto isSkipped = [this](const QString& inputUid) {
        const auto& skipped = m_cc.excludedInputUids;
        return std::find(skipped.begin(), skipped.end(), inputUid.toStdString()) != skipped.end();
    };

    switch (m_objects->subject()) {
    case ObjectController::Subject::Strip: {
        int excluded = 0;
        for (int i = 0; i < m_layout.pageCount(); ++i)
            if (isSkipped(m_layout.page(i).inputUid))
                ++excluded;
        m_stripState->showStrip(m_layout.pageCount(), excluded, m_cc);
        ui->objectProperties->setCurrentWidget(m_stripPage);
        return;
    }
    case ObjectController::Subject::Page: {
        const int i = m_layout.pageForAnchor(m_objects->selectedPage());
        if (i < 0)
            break;
        const Page& page = m_layout.page(i);
        m_stripState->showPage(page.inputUid,
                               tr("p.%1 — %2").arg(i + 1, 2, 10, QLatin1Char('0'))
                                              .arg(QFileInfo(page.sourcePath).fileName()),
                               page.size, isSkipped(page.inputUid),
                               !Platemaker::Models::isNeutral(m_cc));
        ui->objectProperties->setCurrentWidget(m_stripPage);
        return;
    }
    case ObjectController::Subject::None:
    case ObjectController::Subject::Overlay:
    case ObjectController::Subject::Tail:
        break;
    }

    // Which of the two object panels: the kind decides, as it decides everything else since §26.
    if (m_objects->selectionIsArtwork()) {
        m_assetState->showArtwork(m_objects->selectedArtworkName(),
                                  m_objects->selectedArtworkPercent(),
                                  m_objects->selectedArtworkRecord());
        m_assetState->setSelectionBlend(m_objects->selectionBlend());
        ui->objectProperties->setCurrentWidget(m_assetPage);
        return;
    }
    ui->objectProperties->setCurrentWidget(m_objectPage);
}

void Editor::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    applyGrade(cc);
    if (m_gradePanel)
        m_gradePanel->setColourCorrection(cc);   // the project is the source here — show it in the panel
}

bool Editor::gradeActive() const  { return m_pages->gradeActive(); }
QPixmap Editor::gradedOf(int index) const { return m_pages->gradedOf(index); }
QPixmap Editor::pageOf(int index) const   { return m_pages->pageOf(index); }
QPixmap Editor::proxyOf(int index) const  { return m_pages->proxyOf(index); }

void Editor::refreshGradePreview()
{
    m_pages->clearGraded();    // the grade changed → previous previews are stale
    if (m_item)
        m_item->update();
    updateVisiblePages();      // re-grade what's on screen (produceGraded runs for visible pages)
}

Editor::~Editor()
{
    storeSplitterState();
    delete ui;
}

namespace {
//! One QSettings key per splitter. Prefixed, because the strip editor is not the only thing in here.
// The layout's version is in the key. A default that changes is only a default for whoever has never
// moved the splitter — everyone else has a saved state that would go on winning. In case new layout components are added,
// bumps this, so the old entries are ignored, and the user's own adjustments start again from the new
// proportions rather than from a sliver.
QString splitterKey(const QString& name) { return QStringLiteral("stripEditor/splitter/") + name; }
}

void Editor::restoreSplitterState()
{
    QSettings st;
    for (const auto& [name, splitter] : {std::pair{QStringLiteral("editorBody"), ui->editorBody},
                                         std::pair{QStringLiteral("toolColumn"), ui->toolColumn},
                                         std::pair{QStringLiteral("rightPanel"), ui->rightPanel}}) {
        const QByteArray state = st.value(splitterKey(name)).toByteArray();
        if (!state.isEmpty())
            splitter->restoreState(state);
    }
}

void Editor::storeSplitterState() const
{
    QSettings st;
    st.setValue(splitterKey(QStringLiteral("editorBody")), ui->editorBody->saveState());
    st.setValue(splitterKey(QStringLiteral("toolColumn")), ui->toolColumn->saveState());
    st.setValue(splitterKey(QStringLiteral("rightPanel")), ui->rightPanel->saveState());
}

void Editor::setPreviewSource(const std::vector<Platemaker::Models::InputFile>&     inputs,
                                   const Platemaker::Models::OutputProfile&              outProfile,
                                   const std::vector<Platemaker::Models::CanvasProfile>& canvasProfiles,
                                   const std::vector<std::string>&                       canvasProfileIds,
                                   const QString&                                        cacheDir)
{
    // An unchanged feed keeps the built pages — but an empty layout still needs a rebuild, which is the
    // case on the very first feed and after a failed one.
    if (!m_pages->setFeed(inputs, outProfile, canvasProfiles, canvasProfileIds, cacheDir)
        && !m_layout.isEmpty())
        return;
    rebuildScene();
}

void Editor::rebuildScene()
{
    m_objects->setSyncing(true);  // scene->clear() drops the selection; that is not a user action
    m_scene->clear();           // deletes every item (incl. the StripItem); the tracked pointers are now stale
    m_objects->setSyncing(false);
    m_item = nullptr;
    m_seamItems.clear();
    m_objects->forgetItems();   // owned by the scene — already deleted, just forget them
    m_layout.clear();
    m_pages->reset();

    // Ask the library where each page lands. This reads headers and decodes nothing, and the numbers
    // come from the same page-domain code a render uses — so the strip laid out here is the strip a
    // render would build, before any render exists.
    // Pages the render would skip are dropped here, which is what keeps every page below them at the
    // offset the render will give it.
    m_layout.build(m_pages->layoutPages(), m_pages->inputs());

    if (m_layout.isEmpty()) {
        showEmptyState();
        return;
    }

    m_scene->setSceneRect(0, 0, m_layout.stripWidth(), m_layout.stripHeight());
    // One item draws every page as its own image — no per-item seam. It pulls pixels lazily from us.
    m_item = new StripItem(this);
    m_scene->addItem(m_item);
    addSeamItems();

    // Overlays are placed against the layout above, so they can only be built once it exists. The
    // scene's selection did not survive clear(), so re-apply it to whatever is still selected.
    m_objects->syncItems();
    m_objects->refreshList();
    m_objects->reselect();

    // Default view: 100%. Re-applied on resize until the user zooms.
    m_pendingFit = true;
    applyDefaultZoom();
    updateVisiblePages();       // start building what's on screen
}

void Editor::addSeamItems()
{
    // Where the output will be cut. Unlike the page joins, these are not visible in the strip itself,
    // and they are exactly what an author needs to see: a bubble that straddles one lands on both
    // slices. The render cuts every sliceHeight from the top of the strip, so that is what is drawn.
    const int step = m_pages->sliceHeight();
    if (step <= 0)
        return;

    // A thin, semi-transparent guide, in the palette's text colour so it reads in both themes without a
    // hardcoded colour.
    QColor c = palette().color(QPalette::WindowText);
    c.setAlpha(90);
    QPen pen(c);
    pen.setCosmetic(true);      // stays 1px regardless of zoom
    for (int top = step; top < m_layout.stripHeight(); top += step) {
        auto *seam = m_scene->addLine(0, top, m_layout.stripWidth(), top, pen);
        seam->setVisible(ui->buttonSeams->isChecked());
        seam->setZValue(1);     // above the strip item
        m_seamItems.append(seam);
    }
}

void Editor::showEmptyState()
{
    m_objects->setSyncing(true);
    m_scene->clear();
    m_objects->setSyncing(false);
    m_item = nullptr;
    m_seamItems.clear();
    m_objects->forgetItems();
    m_layout.clear();

    auto *text = m_scene->addSimpleText(
        tr("No pages to show.\nAdd input pages to the project to see the strip."));
    text->setBrush(palette().color(QPalette::WindowText));
    const QRectF b = text->boundingRect();
    m_scene->setSceneRect(b.adjusted(-40, -40, 40, 40));
    resetZoom();
}

// ---------------------------------------------------------------------------
// Lazy page build: proxy (blurry, instant) + the real page domain (sharp, async)
// ---------------------------------------------------------------------------

void Editor::updateVisiblePages()
{
    if (m_layout.isEmpty())
        return;

    // The viewport mapped into scene coordinates → which pages intersect it.
    const QRectF vis = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    int first = -1, last = -1;
    for (int i = 0; i < m_layout.pageCount(); ++i) {
        const Page& p = m_layout.page(i);
        if (p.top + p.size.height() >= vis.top() && p.top <= vis.bottom()) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0)
        return;

    first = qMax(0, first - k_prefetchPages);
    last  = qMin(m_layout.pageCount() - 1, last + k_prefetchPages);
    for (int i = first; i <= last; ++i) {
        m_pages->request(i);
        m_pages->produceGraded(i); // grade now if built; otherwise the build watcher grades it on arrival
    }
}

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------

void Editor::updateCursor()
{
    const Tool* tool = toolById(m_tool);
    if (!tool || !m_view || !m_view->viewport())
        return;
    PointerTarget target = PointerTarget::BareStrip;
    if (m_objects && m_pointerPos.x() >= 0)
        target = m_objects->pointerTargetAt(m_view->mapToScene(m_pointerPos), m_view->transform());
    m_view->viewport()->setCursor(cursorFor(*tool, target));
}

void Editor::applyZoom(double z)
{
    m_zoom = qBound(0.02, z, 8.0);
    QTransform t;
    t.scale(m_zoom, m_zoom);
    m_view->setTransform(t);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100.0)));
    updateVisiblePages();       // zoom changes how many pages are on screen
    updateCursor();             // ...and what sits under a pointer that never moved
}

void Editor::userZoom(double z)
{
    m_pendingFit = false;   // the user has taken control — stop re-fitting on resize
    applyZoom(z);
}

void Editor::zoomIn()  { userZoom(m_zoom * 1.25); }
void Editor::zoomOut() { userZoom(m_zoom / 1.25); }
void Editor::resetZoom() { userZoom(1.0); }

void Editor::applyDefaultZoom()
{
    // Default view is native size — 100% — always. (The former "shrink to fit the viewport width" default
    // computed a tiny zoom while the splitter had not yet given the canvas its real width, so the strip
    // opened at ~3%. 100% is what's wanted regardless; "Fit width" stays available on demand.)
    applyZoom(1.0);
}

void Editor::fitWidth()
{
    if (m_layout.stripWidth() <= 0)
        return;
    // Explicit user fit — unlike the default, this MAY enlarge past 100% to fill the width.
    const int vw = m_view->viewport()->width() - m_view->verticalScrollBar()->width();
    if (vw > 0)
        userZoom(static_cast<double>(vw) / static_cast<double>(m_layout.stripWidth()));
}

void Editor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_pendingFit)
        applyDefaultZoom();     // settle the default zoom as the viewport gets its real size
    updateVisiblePages();
}

bool Editor::eventFilter(QObject *watched, QEvent *event)
{
    // **A picture dropped on the strip is placed where it was dropped, at its own size.** Dragged out
    // of ④'s preview, or straight from a file manager — both arrive as a file URL, so one handler
    // serves both and neither needs a tool to be armed.
    if (watched == m_view->viewport()
        && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto* de = static_cast<QDragMoveEvent*>(event);
        if (!droppedArtwork(de->mimeData()).isEmpty()) {
            de->setDropAction(Qt::CopyAction);
            de->accept();
            return true;
        }
    }
    if (watched == m_view->viewport() && event->type() == QEvent::Drop) {
        auto*         drop = static_cast<QDropEvent*>(event);
        const QString file = droppedArtwork(drop->mimeData());
        if (!file.isEmpty() && m_objects) {
            m_objects->placeArtworkAt(file, m_view->mapToScene(drop->position().toPoint()));
            drop->acceptProposedAction();
            return true;
        }
    }

    // **The tools are always all visible.** A flow layout's minimum is one tile, so a splitter was free
    // to shorten the rail until the last row of tools was simply not drawn — and a tool you cannot see
    // is a tool you do not know you have. The rail's minimum height is therefore whatever its own
    // wrapping needs at its current width, recomputed whenever that width changes.
    if (watched == m_toolTiles && event->type() == QEvent::Resize) {
        if (QLayout* flow = m_toolTiles->layout()) {
            const int needed = flow->heightForWidth(m_toolTiles->width());
            if (needed > 0 && needed != m_toolTiles->minimumHeight())
                m_toolTiles->setMinimumHeight(needed);
        }
    }

    // The middle button scrolls the strip under **every** tool, so no tool has to give up its left
    // button for something as ordinary as looking somewhere else. Qt's own hand-drag is the left
    // button's, and only the Pan tool arms it.
    if (watched == m_view->viewport()) {
        auto* me = event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove
                           || event->type() == QEvent::MouseButtonRelease
                       ? static_cast<QMouseEvent*>(event)
                       : nullptr;
        if (me && event->type() == QEvent::MouseButtonPress && me->button() == Qt::MiddleButton) {
            m_panFrom = me->position().toPoint();
            m_view->viewport()->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (me && event->type() == QEvent::MouseMove && m_panFrom.x() >= 0) {
            const QPoint now  = me->position().toPoint();
            const QPoint step = now - m_panFrom;
            m_panFrom         = now;
            // Scrollbars take whole steps, so this follows the mouse rather than a remembered origin:
            // there is no fraction left over to drift with.
            m_view->horizontalScrollBar()->setValue(m_view->horizontalScrollBar()->value() - step.x());
            m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->value() - step.y());
            return true;
        }
        if (me && event->type() == QEvent::MouseButtonRelease && me->button() == Qt::MiddleButton) {
            m_panFrom = {-1, -1};
            updateCursor();
            return true;
        }
    }

    // The eyedropper: a press takes the colour that is on the strip there, wherever it lands — over an
    // object as much as over a page, because what is sampled is what is drawn.
    if (watched == m_view->viewport() && isSampling()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            // Left fills the primary half, right the secondary — as every eyedropper does. Ctrl+left
            // does the same as right, for a tablet with one barrel button bound to nothing.
            const bool left  = me->button() == Qt::LeftButton;
            const bool right = me->button() == Qt::RightButton;
            if (left || right) {
                sampleColourAt(m_view->mapToScene(me->position().toPoint()),
                               right || (me->modifiers() & Qt::ControlModifier));
                return true;
            }
        } else if (event->type() == QEvent::ContextMenu) {
            return true;   // the right button is the tool's here, so it opens no menu
        }
    }

    // The colour tool: a press spends the pair on **what is under the pointer** — the lettering, the
    // outline or the fill, decided by the picture rather than by a setting. Shift spends the other half.
    //
    // The left button only. The right one belongs to the context menu, and a tool that quietly took it
    // away would be a mode nobody can see — the same mistake the Text tool made when it stripped the
    // object panel.
    if (watched == m_view->viewport() && isApplying() && m_colours
        && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        // A corner grip and a tail tip belong to the canvas under every tool, and the cursor says so —
        // so a press there resizes or aims rather than painting. The tool gets everything else.
        const bool affordance = isCanvasAffordance(
            m_objects->pointerTargetAt(m_view->mapToScene(me->position().toPoint()),
                                       m_view->transform()));
        if (me->button() == Qt::LeftButton && !affordance) {
            const bool other = me->modifiers() & Qt::ShiftModifier;
            m_objects->applyColourAt(m_view->mapToScene(me->position().toPoint()), m_view->transform(),
                                     other ? m_colours->secondary() : m_colours->primary());
            return true;
        }
    }

    // Bubble / Text: the left button draws a new bubble on empty strip. A press that lands on an
    // existing overlay is left alone, so the item's own move/resize handling still runs.
    if (watched == m_view->viewport() && artifactToolActive()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const QPointF scenePos = m_view->mapToScene(me->position().toPoint());
                if (!m_objects->objectAt(scenePos, m_view->transform())) {
                    m_objects->beginPlacement(scenePos);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove && m_objects->isPlacing()) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_objects->updatePlacement(m_view->mapToScene(me->position().toPoint()));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_objects->isPlacing()) {
            m_objects->finishPlacement();
            return true;
        }
    }

    if (watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_pointerPos = me->position().toPoint();
            // Only while nothing is held: mid-drag the view writes a closed hand, and an object being
            // dragged is not "what the pointer is over" in any useful sense.
            if (me->buttons() == Qt::NoButton)
                updateCursor();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            // **Queued, and it has to be.** Under ScrollHandDrag the view restores an open hand on every
            // left release — even one that never panned, because the press was taken by an item — and its
            // handler runs after this filter. Deciding here would be overwritten a moment later, which is
            // what made a click on a balloon flash the hand until the mouse moved a pixel. Deciding after
            // the event has been handled puts us last again.
            QMetaObject::invokeMethod(this, [this] { updateCursor(); }, Qt::QueuedConnection);
        } else if (event->type() == QEvent::Enter) {
            m_pointerPos = static_cast<QEnterEvent*>(event)->position().toPoint();
            updateCursor();   // coming back onto the canvas is a hover like any other
        } else if (event->type() == QEvent::Leave) {
            m_pointerPos = {-1, -1};
        }
    }

    if (watched == m_view->viewport() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const int d = we->angleDelta().y();
            if (d > 0)
                zoomIn();
            else if (d < 0)
                zoomOut();
            return true;        // consumed — plain wheel still scrolls vertically
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Text & bubbles
//
// The strip's scene *is* the strip at 1:1, so an overlay's scene position is its library placement
// plus its anchor page's top — no coordinate mapping layer, and the preview lands exactly where the
// render will put it.
// ---------------------------------------------------------------------------

void Editor::setOverlaySource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                              const ArtifactMap&                                  artifacts)
{
    m_objects->setSource(overlays, artifacts);
    updateCursor();   // an object may have arrived under, or vanished from beneath, a still pointer
}

void Editor::selectAfterFeed(const QStringList& uids)
{
    m_objects->selectAfterFeed(uids);
}

void Editor::setAdvisories(Advisories* registry, const QString& projectUid)
{
    if (!m_advisoryBar) {
        // Appended to the root column, so it spans the editor's full width under everything else —
        // the same place a window puts a status bar, for the same reason.
        m_advisoryBar = new AdvisoryBar(registry, this);
        // Bottom right, the corner a status bar keeps its permanent items in — so the chips are in the
        // same place whichever window the artist is looking at them in.
        ui->rootLayout->addWidget(m_advisoryBar, 0, Qt::AlignRight);
    }
    m_advisoryBar->setProjectUid(projectUid);
}

void Editor::setAdvisoriesActive(bool active)
{
    if (m_advisoryBar)
        m_advisoryBar->setActive(active);
}

bool Editor::sampleColourAt(const QPointF& scenePos, bool secondary)
{
    if (!m_colours || !m_pages || m_layout.isEmpty())
        return false;
    const int page = m_layout.pageAtSceneY(scenePos.y());
    if (page < 0 || !m_layout.pageRect(page).contains(scenePos))
        return false;   // the gutter between two pages is not a colour anyone means to pick

    // A page still showing its blurry proxy is not sampled: a stand-in would answer with an average of
    // the colours around the point rather than the colour at it. Ask for the real pixels instead.
    if (m_pages->gradedOf(page).isNull() && m_pages->pageOf(page).isNull()) {
        m_pages->request(page);
        return false;
    }

    // **What is drawn is what is picked.** One pixel of the scene, composited: the page through its
    // grade, and every balloon, caption and imported asset over it, each with its own blend mode and
    // opacity — the same pixels the render will produce. Sampling the page pixmap alone was defensible
    // and still wrong: clicking a balloon gave the paper behind it.
    //
    // Two things in the scene are the editor talking rather than the comic, and they are hidden for the
    // one repaint: selection chrome, and the seam guides.
    QList<QGraphicsLineItem*> hiddenSeams;
    for (QGraphicsLineItem* seam : std::as_const(m_seamItems)) {
        if (seam->isVisible()) {
            seam->setVisible(false);
            hiddenSeams.append(seam);
        }
    }
    Object::setChromeVisible(false);

    QImage pixel(1, 1, QImage::Format_ARGB32);
    pixel.fill(Qt::transparent);
    {
        QPainter p(&pixel);
        m_scene->render(&p, QRectF(0, 0, 1, 1),                       // the pixel under the cursor,
                        QRectF(scenePos - QPointF(0.5, 0.5), QSizeF(1, 1)),   // not the one past it
                        Qt::IgnoreAspectRatio);
    }

    Object::setChromeVisible(true);
    for (QGraphicsLineItem* seam : std::as_const(hiddenSeams))
        seam->setVisible(true);

    const QColor picked = pixel.pixelColor(0, 0);
    if (picked.alpha() == 0)
        return false;   // nothing was drawn there after all
    m_colours->set(picked, secondary);
    return true;
}


QString Editor::droppedArtwork(const QMimeData* mime)
{
    // A picture, by what it *is* rather than by where it came from: the same three suffixes the import
    // has always taken. Anything else — a page, a workspace, a folder — is not for this canvas.
    if (!mime || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        for (const char* ext : {".svg", ".png", ".webp"})
            if (path.endsWith(QLatin1String(ext), Qt::CaseInsensitive))
                return path;
    }
    return {};
}

bool Editor::artifactToolActive() const
{
    const Tool* tool = toolById(m_tool);
    return tool && tool->kind == ToolKind::Create;
}

bool Editor::isSampling() const
{
    const Tool* tool = toolById(m_tool);
    return tool && tool->kind == ToolKind::Sample;
}

bool Editor::isApplying() const
{
    const Tool* tool = toolById(m_tool);
    return tool && tool->kind == ToolKind::Apply;
}

void Editor::refreshGeneratedToolIcons()
{
    if (!m_toolGroup)
        return;
    for (int i = 0; i < tools().size(); ++i) {
        const Tool& t = tools().at(i);
        if (!t.icon.isEmpty())
            continue;
        auto* b = m_toolGroup->button(i);
        if (b && t.shape)
            b->setIcon(QIcon(shapeThumbnail(*t.shape, palette())));
    }
}

void Editor::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange)
        refreshGeneratedToolIcons();
}


}  // namespace StripEdit
