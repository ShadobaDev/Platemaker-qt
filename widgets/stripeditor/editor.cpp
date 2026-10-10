#include "editor.hpp"
#include "ui_editor.h"
#include "advisorybar.hpp"
#include "properties/colouradjustment.hpp"
#include "toolrail/colourpair.hpp"
#include "toolrail/cursors.hpp"
#include "tooloptions/gradetooloptions.hpp"
#include "objects/object.hpp"
#include "objects/objectcontroller.hpp"
#include "objects/placement.hpp"
#include "canvas/pagesource.hpp"
#include "canvas/canvasinput.hpp"
#include "canvas/sampling.hpp"
#include "canvas/stripitem.hpp"
#include "toolrail/toolrail.hpp"
#include "tooloptions/tooloptionsstack.hpp"
#include "objectstate/objectstate.hpp"
#include "objectstack/objectmenu.hpp"
#include "objectstack/objectstack.hpp"
#include "objectstate/objectstatestack.hpp"
#include "presetstore.hpp"
#include "properties/shapeeditor.hpp"
#include "objectstate/stripstate.hpp"
#include "tooloptions/artworktooloptions.hpp"
#include "tooloptions/bubbletooloptions.hpp"

#include <platemaker/models/colour_correction.hpp>

#include <algorithm>

#include <QDebug>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyle>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace StripEdit {

namespace {

/**
 * The key for the tool-options page whose contents decide **which picture** a placement puts down.
 * Three places need to name it: the page's own registration, and the two moments the editor has to
 * know that a picture rather than a balloon is what the next drag will place. Those two used to ask
 * `m_tool == "artwork"` — the tool's *id*, which the registry exists so that nothing outside it has
 * to know. Which options page a tool shows is a property of its row, and it is the question actually
 * being asked.
 */
constexpr QLatin1String k_artworkPage{"artwork"};
/**
 * @brief How wide a splitter's grab area is. Wider than the default 1 point, which was as hard to hit as it sounds.
 */
constexpr int k_splitterHandlePx = 6;

/**
 * @brief The least the tool's options may be squeezed to. Below this the panel is a heading with no controls under it, which says less than nothing about what the armed tool will do.
 */
constexpr int k_toolOptionsFloorPx = 140;

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
 * @brief Pages built beyond the viewport on each side. One page is several slices tall, so ±1 already covers a comfortable scroll ahead; a larger margin would multiply a much heavier unit of work.
 */
constexpr int k_prefetchPages = 1;

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

/**
 * @brief Makes @p splitter's handles visible, in the palette's own colours.
 *
 * Neither the native style nor Fusion paints anything on a splitter handle (checked, both), so widening
 * one only widens the gap. Filling it does say where the seam is — and which of the two neighbouring
 * greys reads as a line depends on the theme, so it is chosen the same way `widgets/badge/` chooses a
 * chip's lightness: against the window's own.
 * 
 * @param splitter The splitter to show the handles of.
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

} // namespace

Editor::Editor(PresetStore& presets, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Editor)
    , m_presets(&presets)
{
    // Page memory: the feed, the proxy/sharp tiers and the graded previews. It reads m_layout, which
    // this widget owns and rebuilds, so it takes it by reference.
    m_pages = new PageSource(m_layout, this);
    connect(m_pages, &PageSource::pageReady, this, [this](int index) {
        if (m_item)
            m_item->update(m_layout.pageRect(index));
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
    // Without this a widget hears about the mouse only while a button is held, which is what made the
    // cursor look as though it followed the last *click*: it could only be re-decided on release. The
    // cursor is a promise about what a press would do here, so it has to be re-decided while hovering.
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->setAcceptDrops(true);   // pictures, from the TOOL OPTIONS preview or a file manager
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

    // --- Editor shell: the splitters, canvas, tool-options stack and object stack come from the .ui
    // (editorBody = toolbox | canvas | rightPanel). Here we only fill the toolbox with a flowing grid of
    // square tool tiles (a flow layout can't be expressed in a .ui), give the stack a page per tool, and
    // set the splitter sizing (not a .ui property). Pan (the default) keeps today's behaviour: hand-drag
    // pan and no side panel.
    {
        // TOOL RAIL: the tiles, one per row of the registry, and the colour pair under them.
        m_rail    = new ToolRail(ui->toolRail);
        m_colours = m_rail->colours();

        // TOOL OPTIONS: a page per options-page key, plus the hint page for a tool that has none. The
        // pages are built and wired here; the stack only holds them and knows which one a tool shows.
        m_toolOptions = new ToolOptionsStack(ui->toolOptionsStack);
        // The Grade tool's options are an image editor's colour menu: which adjustment, and its controls, applied to the
        // selected object. What a selected strip's grade *is* is shown on the right, in its state.
        m_gradeOptions = new GradeToolOptions(ui->toolOptionsStack);
        m_toolOptions->addPage(QStringLiteral("grade"), m_gradeOptions);
        connect(m_gradeOptions, &GradeToolOptions::changed, this, [this](const Platemaker::Models::ColourCorrection& cc) {
            // Live edit: apply it, but do NOT push it back into the panel — the panel is the source
            // here, and re-syncing its widgets mid-drag would fight the slider the user is holding.
            applyGrade(cc);
        });
        connect(m_gradeOptions, &GradeToolOptions::committed, this,
                [this](const Platemaker::Models::ColourCorrection& cc, const QString& undoText) {
            emit colourCorrectionCommitted(cc, undoText);   // settled: the owner persists it, one undo step
        });
        // The panel can only act on the strip; asked for it, it gets it.
        connect(m_gradeOptions, &GradeToolOptions::selectStripRequested, this, [this] {
            if (m_objects)
                m_objects->selectStrip();
        });
        // One panel for every tool that authors an `ObjectRecord` — Bubble, Text, Caption — because they
        // author the same object and differ only in the shape they place, which each tool's row says.
        m_bubbleOptions = new BubbleToolOptions(*m_presets, ui->toolOptionsStack);
        m_toolOptions->addPage(QStringLiteral("bubble"), m_bubbleOptions);

        // The other create tool's options: which picture the next placement puts down. A separate panel
        // rather than a section of the one above, because they describe different kinds of object, and an
        // options page describes one kind at a time.
        m_artworkOptions = new ArtworkToolOptions(ui->toolOptionsStack);
        m_toolOptions->addPage(k_artworkPage, m_artworkOptions);
        connect(m_artworkOptions, &ArtworkToolOptions::artworkChanged, this, [this](const QString& f) {
            const Tool* armed = toolById(m_tool);
            if (m_placement && armed && armed->optionsPage == k_artworkPage)
                m_placement->setArtwork(f);
        });

        m_toolOptions->assertEveryToolHasAPage();

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
        m_objectState = new ObjectState(*m_presets, ui->objectStateStack);
        // The strip and its pages are selected too, and they are not overlays — so they get the same
        // surface with different contents rather than an object panel full of sections that never apply.
        m_stripState  = new StripState(ui->objectStateStack);
        m_stateStack  = new ObjectStateStack(ui->objectStateStack, m_objectState, m_stripState);

        connect(m_stripState, &StripState::excludedToggled, this,
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
            emit colourCorrectionCommitted(cc, excluded ? tr("Exclude page from colour correction")
                                                     : tr("Include page in colour correction"));
        });
        // An adjustment listed on the strip: reopened in the Grade tool, or taken off the strip.
        connect(m_stripState, &StripState::adjustmentEditRequested, this, [this](ColourAdjustment a) {
            setTool(QStringLiteral("grade"));
            m_gradeOptions->openAdjustment(a);
        });
        connect(m_stripState, &StripState::adjustmentRemoveRequested, this, [this](ColourAdjustment a) {
            emit colourCorrectionCommitted(withoutColourAdjustment(m_cc, a),
                                        tr("Remove %1").arg(colourAdjustmentName(a)));
        });

        // Everything placed on the strip. It drives the scene, the list and the panel; it owns no
        // persistence, so every edit leaves through one of its four signals and comes back as a re-feed.
        m_objects = new ObjectController(m_scene, m_view, *m_presets, m_layout, this);
        // OBJECT STATE and the objects, both ways. The panel's edits go to the objects it was bound to;
        // the controller says what it is bound to now. Neither includes the other — they meet here.
        connect(m_objectState, &ObjectState::changed, m_objects,
                [this](const ObjectRecord& a) { m_objects->applyPanelRecords({a}, /*commit=*/false); });
        connect(m_objectState, &ObjectState::committed, m_objects,
                [this](const ObjectRecord& a) { m_objects->applyPanelRecords({a}, /*commit=*/true); });
        connect(m_objectState, &ObjectState::blendPicked, m_objects, &ObjectController::setSelectionBlend);
        connect(m_objectState, &ObjectState::deleteRequested, m_objects,
                &ObjectController::deleteSelectedOverlay);
        connect(m_objectState, &ObjectState::changedMany, m_objects,
                [this](const QList<ObjectRecord>& objects) {
            m_objects->applyPanelRecords(objects, /*commit=*/false);
        });
        connect(m_objectState, &ObjectState::committedMany, m_objects,
                [this](const QList<ObjectRecord>& objects) {
            m_objects->applyPanelRecords(objects, /*commit=*/true);
        });
        connect(m_objectState, &ObjectState::scaleChanged, m_objects,
                [this](double percent) { m_objects->scaleSelectedArtwork(percent, /*commit=*/false); });
        connect(m_objectState, &ObjectState::scaleCommitted, m_objects,
                [this](double percent) { m_objects->scaleSelectedArtwork(percent, /*commit=*/true); });
        connect(m_objectState, &ObjectState::fitRequested, m_objects,
                &ObjectController::fitSelectionToText);
        using OC = ObjectController;
        using OS = ObjectState;
        connect(m_objects, &OC::boundToRecord,      m_objectState, &OS::setRecord);
        connect(m_objects, &OC::boundToTail,        m_objectState, &OS::setTail);
        connect(m_objects, &OC::boundToRecords,     m_objectState, &OS::setRecords);
        connect(m_objects, &OC::boundToMixed,       m_objectState, &OS::setMixedSubjects);
        connect(m_objects, &OC::boundToNothing,     m_objectState, &OS::clearSelection);
        connect(m_objects, &OC::blendBound,         m_objectState, &OS::setSelectionBlend);
        connect(m_objects, &OC::artworkScaleBound,  m_objectState, &OS::setArtworkScale);
        connect(m_objects, &OC::textFocusRequested, m_objectState, &OS::focusText);
        // What TOOL OPTIONS says the next object is, for *Apply from tool options ▸* and *Convert to ▸
        // Balloon*. Told as functions, so the objects need no tool-options panel.
        m_objects->setToolDefaults({[this] { return m_bubbleOptions->prototype(); },
                                    [this] { return m_bubbleOptions->balloonShape(); }});
        // A Create tool's drag on empty strip, and the object it places: the balloon TOOL OPTIONS
        // describes, or the picture the Artwork tool is armed with.
        m_placement = new Placement(m_scene, *m_objects, m_layout, this, this);
        m_placement->setPrototype([this] { return m_bubbleOptions->prototype(); });
        // OBJECT STACK: the rows. A view of the controller, built before any feed can arrive.
        m_objectStack = new ObjectStack(ui->objectStack, *m_objects, m_layout, *m_presets, this, this);
        // The object menu, on the stack and the canvas alike. It spends the same pair the bucket does —
        // it reads it, never writes it.
        m_objectMenu = new ObjectMenu(*m_objects, m_layout, *m_presets, ui->objectStack, m_view, this, this);
        m_objectMenu->setColourSource(m_colours);
        connect(m_objects, &ObjectController::recordCreated,        this, &Editor::recordCreated);
        connect(m_objects, &ObjectController::overlaysCommitted,         this, &Editor::overlaysCommitted);
        connect(m_objects, &ObjectController::artworkImportRequested, this, &Editor::artworkImportRequested);
        connect(m_objects, &ObjectController::subjectChanged,         this, [this] { showSubject(); });
        connect(m_objects, &ObjectController::noted,                  this, &Editor::noted);

        // CANVAS: what a press, a drag, a drop or a wheel on the strip does under the armed tool. It does
        // the canvas's own part and reports the rest, which is wired here because it belongs elsewhere:
        // the colour pair to the rail, zoom to this shell, a dropped picture to the objects' import.
        m_input = new CanvasInput(m_view, m_objects, m_placement, this);
        connect(m_input, &CanvasInput::artworkDropped, this, [this](const QString& file, QPointF at) {
            m_objects->placeArtworkAt(file, at);
        });
        connect(m_input, &CanvasInput::sampleRequested, this, [this](QPointF at, bool secondary) {
            if (!m_colours || !m_pages)
                return;
            if (const auto picked = sampleCanvas(*m_scene, m_layout, *m_pages, m_seamItems, at))
                m_colours->set(*picked, secondary);
        });
        connect(m_input, &CanvasInput::colourApplyRequested, this, [this](QPointF at, bool secondary) {
            if (m_colours)
                m_objects->applyColourAt(at, m_view->transform(),
                                         secondary ? m_colours->secondary() : m_colours->primary());
        });
        connect(m_input, &CanvasInput::zoomStepRequested, this, [this](int direction) {
            if (direction > 0)
                zoomIn();
            else
                zoomOut();
        });
        // Splitter behaviour (not expressible in the .ui): canvas absorbs resize, panels keep their width.
        ui->editorBody->setStretchFactor(0, 0);   // toolbox
        ui->editorBody->setStretchFactor(1, 1);   // canvas
        ui->editorBody->setStretchFactor(2, 0);   // right panel
        // Neither side column can be dragged under what its panel needs. The tool column knows that
        // because its panel is complete; the right column is told, because its panel is not yet.
        const int toolW = qMax(k_rightColumnPx, columnWidthFor(m_bubbleOptions));
        ui->toolColumn->setMinimumWidth(toolW);
        ui->rightPanel->setMinimumWidth(k_rightColumnPx);
        ui->editorBody->setChildrenCollapsible(false);   // no column can be dragged out of existence
        ui->editorBody->setSizes({toolW, 700, k_rightColumnPx});
        ui->toolColumn->setStretchFactor(0, 0);    // the tile rail takes what it needs
        ui->toolColumn->setStretchFactor(1, 1);    // the options absorb the rest
        // The tools are never negotiable — the rail's minimum follows its own wrapping (see
        // ToolRail) — and the options keep a floor of their own, so neither can be shut by a drag.
        ui->toolOptionsStack->setMinimumHeight(k_toolOptionsFloorPx);
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

        connect(m_rail, &ToolRail::toolPicked, this, &Editor::setTool);

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
    m_rail->setCurrent(m_tool);
    m_toolOptions->show(*tool);
    if (m_input)
        m_input->setTool(*tool);

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
    if (m_input)
        m_input->updateCursor();

    // Only the *tool's own options* follow the tool: a tool that places one shape says so, and the
    // Bubble tool leaves the choice on the tiles. This replaced two gates that each existed to say
    // "this tool makes a shapeless object" — one here, one in the object controller.
    //
    // The object-state panel deliberately gets no such call. It describes whatever is **selected**, and
    // a tool chosen minutes ago must not decide what a bubble's properties look like — selecting a
    // balloon under the Text tool used to show nothing but its lettering, an effect with no visible
    // cause. Which groups that panel shows comes from the selected object's own kind instead.
    if (m_bubbleOptions)
        m_bubbleOptions->setToolShape(tool->kind == ToolKind::Create ? tool->shape : std::nullopt);

    // What a placement puts down: a picture, or — when this is empty — a balloon. Told to the placement at
    // the moment the tool is armed.
    if (m_placement) {
        QString artwork;
        if (tool->optionsPage == k_artworkPage && m_artworkOptions) {
            // Arming with nothing chosen asks once, here: before any drag, so a file dialog never lands
            // in the middle of one. A cancelled dialog arms nothing, and TOOL OPTIONS says as much.
            if (m_artworkOptions->artwork().isEmpty())
                m_artworkOptions->chooseArtwork();
            artwork = m_artworkOptions->artwork();
        }
        m_placement->setArtwork(artwork);
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
        m_objectStack->setExcludedPages(skipped);
    }
    showSubject();   // a selected strip or page describes the grade, so it follows it
    if (m_pages->setColourCorrection(cc))
        refreshGradePreview();
}

void Editor::showSubject()
{
    if (!m_objects || !m_stateStack)
        return;

    // The Grade tool acts on the selection, so it is told what that is whether or not it is showing.
    if (m_gradeOptions) {
        const auto s = m_objects->subject();
        m_gradeOptions->setTarget(s == ObjectController::Subject::Strip ? GradeToolOptions::Target::Strip
                                : s == ObjectController::Subject::Page ? GradeToolOptions::Target::Page
                                                                         : GradeToolOptions::Target::Other);
    }

    // Which panel answers is the selection's to decide; what it says is the stack's.
    switch (m_objects->subject()) {
    case ObjectController::Subject::Strip:
        m_stateStack->showStrip(m_layout, m_cc);
        return;
    case ObjectController::Subject::Page: {
        const int i = m_layout.pageForAnchor(m_objects->selectedPage());
        if (i < 0)
            break;
        m_stateStack->showPage(m_layout, i, m_cc);
        return;
    }
    case ObjectController::Subject::None:
    case ObjectController::Subject::Overlay:
    case ObjectController::Subject::Tail:
        break;
    }
    m_stateStack->showObject();
}

void Editor::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    applyGrade(cc);
    if (m_gradeOptions)
        m_gradeOptions->setColourCorrection(cc);   // the project is the source here — show it in the panel
}

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
    // Children go in the order they were made, so the object list (ui's) is destroyed before the scene —
    // and a scene removing a selected item announces a selection change on its way out, which the
    // controller would answer by writing to that list. Nothing the scene says during teardown is for anyone.
    m_scene->blockSignals(true);
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
    // The objects' records are read *before* the scene deletes them: forgetItems() harvests them, and
    // after clear() every tracked pointer is dangling.
    m_objects->forgetItems();
    m_objects->setSyncing(true);  // scene->clear() drops the selection; that is not a user action
    m_scene->clear();           // deletes every item (incl. the StripItem)
    m_objects->setSyncing(false);
    m_item = nullptr;
    m_seamItems.clear();
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
    // One item draws every page as its own image — no per-item seam. It pulls pixels lazily from the
    // page memory, and the editor's palette for a page that has none yet.
    m_item = new StripItem(m_layout, *m_pages, this);
    m_scene->addItem(m_item);
    addSeamItems();

    // Overlays are placed against the layout above, so they can only be built once it exists. The
    // scene's selection did not survive clear(), so re-apply it to whatever is still selected.
    m_objects->syncItems();
    m_objectStack->refresh();
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
    m_objects->forgetItems();   // before clear(), for the reason given in rebuildScene()
    m_objects->setSyncing(true);
    m_scene->clear();
    m_objects->setSyncing(false);
    m_item = nullptr;
    m_seamItems.clear();
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
    // A closed editor builds nothing: it gave its pages back when it closed (hideEvent) and asks for the
    // ones on screen again when it is shown. A tab behind another is not closed, and keeps building.
    if (!isVisible())
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

void Editor::applyZoom(double z)
{
    m_zoom = qBound(0.02, z, 8.0);
    QTransform t;
    t.scale(m_zoom, m_zoom);
    m_view->setTransform(t);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100.0)));
    updateVisiblePages();       // zoom changes how many pages are on screen
    if (m_input)
        m_input->updateCursor();   // ...and what sits under a pointer that never moved
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

void Editor::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // A closed editor gives its pages back — a long strip's are hundreds of MiB, and a closed editor is
    // only hidden (its project still needs it). Not on a minimised window (spontaneous), and not on the
    // hide Qt does in passing while a dock floats, docks or maximises: that one is shown again before the
    // event loop turns, so the release waits for it and lets a window still in use keep its pages.
    if (event->spontaneous())
        return;
    QTimer::singleShot(0, this, [this] {
        if (!isVisible())
            m_pages->reset();
    });
}

void Editor::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateVisiblePages();   // what a closed editor gave back, and what it skipped while closed
}

void Editor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_pendingFit)
        applyDefaultZoom();     // settle the default zoom as the viewport gets its real size
    updateVisiblePages();
}

// ---------------------------------------------------------------------------
// Text & bubbles
//
// The strip's scene *is* the strip at 1:1, so an overlay's scene position is its library placement
// plus its anchor page's top — no coordinate mapping layer, and the preview lands exactly where the
// render will put it.
// ---------------------------------------------------------------------------

void Editor::setOverlaySource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                              const ObjectRecord::Map&                                  records)
{
    m_objects->setSource(overlays, records);
    if (m_input)
        m_input->updateCursor();   // an object may have arrived under, or vanished from beneath, a still pointer
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



}  // namespace StripEdit
