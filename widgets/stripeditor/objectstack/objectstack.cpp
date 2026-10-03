#include "objectstack/objectstack.hpp"

#include "badge.hpp"
#include "badgeitemdelegate.hpp"
#include "objects/artworkobject.hpp"
#include "objects/object.hpp"
#include "objects/objectcontroller.hpp"
#include "objects/striplayout.hpp"
#include "objectstack/rowglyph.hpp"
#include "presetstore.hpp"
#include "properties/blendeditor.hpp"   // blendName(): a row names a blend mode the way the menu does
#include "recordpainter.hpp"

#include <QAbstractItemModel>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPalette>
#include <QTimer>
#include <QTreeWidget>

namespace StripEdit {

ObjectStack::ObjectStack(QTreeWidget* tree, ObjectController& objects, const StripLayout& layout,
                         PresetStore& presets, const QWidget* palette, QObject* parent)
    : QObject(parent)
    , m_tree(tree)
    , m_objects(objects)
    , m_layout(layout)
    , m_presets(presets)
    , m_palette(palette)
{
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    // Extended: Ctrl adds, Shift takes a run — the two gestures every list in the application uses.
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    // Branch decoration, because the strip nests its pages. Rows start collapsed; what the artist opens
    // stays open, since the rows are updated in place rather than rebuilt.
    m_tree->setRootIsDecorated(true);
    // The row grows to fit the glyph: a column of objects, not of labels. Icons are drawn at the screen's
    // own density (see rowglyph.cpp), so this is a size in points and not a reason for anything to be
    // scaled up afterwards.
    m_tree->setIconSize(QSize(k_rowGlyphPx, k_rowGlyphPx));
    // A row names its object; the chips after the name say what is true of it. The delegate hands any
    // row with nothing to report straight back to the style, so tails, pages and the strip are drawn
    // exactly as they were.
    m_tree->setItemDelegate(new BadgeItemDelegate(m_tree));

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        // A drag moves a row by taking it out and putting it back, and the taking clears its selection.
        // That is the tree's mechanics, not the artist deselecting, so the selection stands until the
        // move has been committed and re-shown.
        if (m_syncing || m_rowsMoving) return;
        const auto sel = m_tree->selectedItems();
        if (sel.isEmpty()) {
            m_objects.selectOverlay(QString());
            return;
        }
        // The strip and a page are one of a kind: picking one collapses whatever set was there. Objects
        // and tails gather together — they share a position, which is what a drag acts on.
        QStringList    overlays;
        QList<ObjectController::TailRef> tails;
        for (const QTreeWidgetItem* row : sel) {
            const QString id = row->data(0, Qt::UserRole).toString();
            switch (static_cast<ObjectController::Subject>(row->data(0, k_kindRole).toInt())) {
            case ObjectController::Subject::Strip: m_objects.selectStrip();  return;
            case ObjectController::Subject::Page:  m_objects.selectPage(id); return;
            case ObjectController::Subject::Tail:  tails.append(ObjectController::TailRef{id, row->data(0, k_tailRole).toInt()}); break;
            default:             overlays.append(id); break;
            }
        }
        m_objects.selectSubjects(overlays, tails);
    });
    // Both list handlers are deferred to the next event-loop turn on purpose. Persisting an edit
    // round-trips through the owner and comes back as a re-feed that clears and refills this list —
    // which cannot safely happen inside the list's own itemChanged / rowsMoved emission.
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* row, int) {
        if (m_syncing || !row) return;
        const QString uid = row->data(0, Qt::UserRole).toString();
        const bool    on  = row->checkState(0) == Qt::Checked;
        QTimer::singleShot(0, this, [this, uid, on] { m_objects.setOverlayEnabled(uid, on); });
    });
    // Dragging a row changes the composite order, which is what the render draws bottom-to-top. Listened
    // for as both a move and an insert: a list reports a drag as a move, a tree as a removal followed by
    // an insertion. Which one arrives is Qt's business, so neither is relied on; either restarts one
    // timer, and the commit it fires compares the rows with the model and does nothing if they agree.
    m_orderCommit = new QTimer(this);
    m_orderCommit->setSingleShot(true);
    m_orderCommit->setInterval(0);
    connect(m_orderCommit, &QTimer::timeout, this, &ObjectStack::commitOrder);
    const auto orderMayHaveChanged = [this] {
        if (!m_syncing)
            m_orderCommit->start();
    };
    connect(m_tree->model(), &QAbstractItemModel::rowsMoved,    this, orderMayHaveChanged);
    connect(m_tree->model(), &QAbstractItemModel::rowsInserted, this, orderMayHaveChanged);
    // Outside a refresh, nothing but a drag removes a row — deleting an object goes through the model
    // and comes back as a refresh.
    connect(m_tree->model(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] {
        if (m_syncing)
            return;
        m_rowsMoving = true;
        m_orderCommit->start();   // so the flag is cleared even if no insertion ever follows
    });

    // What the controller changes, the rows follow. It knows nothing of this tree: it says what changed.
    connect(&m_objects, &ObjectController::stackChanged,             this, &ObjectStack::refresh);
    connect(&m_objects, &ObjectController::rowSelectionChanged,      this, &ObjectStack::showSelectedRows);
    connect(&m_objects, &ObjectController::subjectRowChanged,        this, &ObjectStack::showSubjectRow);
    connect(&m_objects, &ObjectController::revealSelectionRequested, this, &ObjectStack::revealSelectedRow);
}

void ObjectStack::setExcludedPages(const QSet<QString>& inputUids)
{
    if (inputUids == m_excludedPages)
        return;
    m_excludedPages = inputUids;
    refresh();
}

QIcon ObjectStack::rowGlyph(const QString& uid)
{
    // Drawn once per look, not once per feed. The silhouette is cheap but not free — a tail's base is
    // found by casting a ray at the outline — and a feed arrives after every edit, so the key is what the
    // glyph is made of: the record, less the groups a glyph does not draw. Taken from the persisted form
    // rather than listed field by field, because a field that changes the drawing is persisted anyway —
    // a hand-written list missed the stroke width and a tail's width and bend.
    if (!m_objects.isParametric(uid)) {
        // Imported artwork: no authoring record, so no silhouette. The art is its own glyph.
        const auto* art = qobject_cast<const ArtworkObject*>(m_objects.object(uid));
        return art ? assetGlyph(art->artwork(), k_rowGlyphPx, m_tree->devicePixelRatioF()) : QIcon();
    }
    const ObjectRecord a = m_objects.recordFor(uid);

    ObjectRecord drawn  = a;
    drawn.text      = {};   // a glyph is the silhouette; lettering is drawn as a fixed "Aa"
    drawn.style     = {};   // line styles are SVG filters, applied by the render and never here
    drawn.styleSeed = 0;
    QString key = QString::fromUtf8(QJsonDocument(drawn.toJson()).toJson(QJsonDocument::Compact));
    key += QStringLiteral("|@%1").arg(m_tree->devicePixelRatioF());   // a window can move to another screen

    auto it = m_glyphs.constFind(uid);
    if (it != m_glyphs.constEnd() && it->first == key)
        return it->second;

    const QIcon glyph = objectGlyph(a, m_tree->palette(), k_rowGlyphPx, m_tree->devicePixelRatioF());
    m_glyphs.insert(uid, {key, glyph});
    return glyph;
}

void ObjectStack::refresh()
{
    if (!m_tree)
        return;

    m_syncing = true;

    // **Updated in place, never cleared and refilled.** The tree is where an object is picked out
    // precisely, and every edit comes back to this controller as a feed — so a tree rebuilt on each feed
    // would lose what the artist had opened, selected or scrolled to at exactly the moment they were
    // using it. Rows are matched by id — an overlay's uid, the strip's fixed id, a page's input uid — and
    // only what changed is touched.
    QHash<QString, QTreeWidgetItem*> unused;
    for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
        QTreeWidgetItem* row = m_tree->topLevelItem(r);
        unused.insert(row->data(0, Qt::UserRole).toString(), row);
    }

    // Puts @p row at top-level position @p index, moving it only if it is not already there. A move is a
    // take and an insert, and the view forgets whether a taken row was expanded — so that is carried
    // across by hand, or every reorder would fold up whatever the artist had opened.
    const auto placeTopLevel = [this](QTreeWidgetItem* row, int index) {
        const int at = m_tree->indexOfTopLevelItem(row);
        if (at == index)
            return;
        const bool open = at >= 0 && row->isExpanded();
        if (at >= 0)
            m_tree->takeTopLevelItem(at);
        m_tree->insertTopLevelItem(index, row);
        row->setExpanded(open);
    };

    // The tree is a **stack**: row 0 is the front-most object, and a row covers every row below it
    // wherever they overlap. The render draws m_objects.overlays() in vector order, so the *last* element is the
    // one on top — which makes the rows that vector reversed. Reversing here rather than in the model
    // keeps the library's "composite order == vector order" rule intact and costs one iterator.
    int index = 0;
    for (auto rit = m_objects.overlays().rbegin(); rit != m_objects.overlays().rend(); ++rit, ++index) {
        const auto&   o    = *rit;
        const QString uid  = QString::fromStdString(o.uid);
        const int     page = m_layout.pageForAnchor(QString::fromStdString(o.anchorInputUid));

        // The object names itself. The fallback covers the one moment there is no object to ask: rows
        // refreshed before the strip has a layout, where syncItems() has nothing to place anything against.
        const Object* item  = m_objects.object(uid);
        const QString label = item ? item->label()
                                   : (m_objects.isParametric(uid) ? Painter::label(m_objects.recordFor(uid))
                                                        : tr("(imported artwork)"));
        // The page it sits on, when it sits on one. An unanchored object used to carry the word *orphan*
        // where the page number goes; that is what the chip says now, and saying it twice cost the row
        // the width its lettering needs.
        const QString text = (page >= 0)
                                 ? tr("p.%1 · %2").arg(page + 1, 2, 10, QLatin1Char('0')).arg(label)
                                 : label;

        QTreeWidgetItem* row = unused.take(uid);
        if (!row) {
            row = new QTreeWidgetItem;
            row->setData(0, Qt::UserRole, uid);
            row->setData(0, k_kindRole, static_cast<int>(ObjectController::Subject::Overlay));
            // Drops land between rows and never onto one. Nesting is structural — a tail belongs to its
            // balloon — and is not something a drag may create.
            row->setFlags((row->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsDropEnabled);
        }
        placeTopLevel(row, index);

        row->setText(0, text);
        row->setIcon(0, rowGlyph(uid));
        row->setCheckState(0, o.enabled ? Qt::Checked : Qt::Unchecked);
        // Both set on every pass, not only when unanchored: a row is reused, and one that was greyed must
        // stop being greyed the moment its object is re-anchored.
        if (page < 0) {
            row->setForeground(0, m_palette->palette().brush(QPalette::Disabled, QPalette::WindowText));
            row->setToolTip(0, tr("The page this overlay was anchored to is not in the strip. It is kept, "
                                  "and comes back when its page does — the render skips it meanwhile."));
        } else {
            row->setData(0, Qt::ForegroundRole, QVariant());
            row->setToolTip(0, QString());
        }
        // What is true of this object and is written nowhere else on the row. Muting is **not** here:
        // the row's own checkbox already answers it, and a chip repeating a control next to it is a
        // second voice saying the same thing.
        QList<Badge> badges;
        const QPalette pal = m_tree->palette();
        if (page < 0)
            badges << toneBadge(BadgeTone::Warning, tr("unanchored"),
                                tr("The page this object was anchored to is not in the strip, so the "
                                   "render skips it. Re-anchor it from the object's menu."), pal);
        // The preset this object still looks like, when it looks like one. **Named or nothing**: a row
        // is scanned, and *Custom* on every hand-made balloon would be a column of chips reporting that
        // there is nothing to report. OBJECT STATE says *Custom* because there the question was asked.
        if (m_objects.isParametric(uid)) {
            const int preset = m_presets.matching(m_objects.recordFor(uid));
            if (preset >= 0)
                badges << toneBadge(BadgeTone::Neutral, m_presets.presets().at(preset).name,
                                    tr("Every property a preset covers still matches this preset."),
                                    pal);
        }
        // Lower-cased here and Title Case on the menu, deliberately: a menu entry is a command and a
        // chip is a remark, and a column of chips that disagreed about their capitals would read as
        // two kinds of thing.
        if (o.blend != Platemaker::Models::BlendMode::Over)
            badges << toneBadge(BadgeTone::Neutral, blendName(o.blend).toLower(),
                                tr("Composited with the page underneath in this blend mode rather "
                                   "than simply drawn over it."), pal);
        // Set on every pass, empty included: a row is reused, and one that stopped being unanchored
        // must stop saying so.
        row->setData(0, BadgeItemDelegate::k_badgesRole, QVariant::fromValue(badges));

        row->setSelected(m_objects.subject() == ObjectController::Subject::Overlay && m_objects.selectedOverlays().contains(uid));

        // A bubble's tails, one row each, by position: a row stands for whichever tail is at its index now.
        // Only a bubble has tails, so artwork gets none.
        const int tailCount = m_objects.isParametric(uid)
                                  ? static_cast<int>(m_objects.recordFor(uid).tails.items.size()) : 0;
        while (row->childCount() > tailCount)
            delete row->takeChild(row->childCount() - 1);
        for (int t = 0; t < tailCount; ++t) {
            QTreeWidgetItem* tailItem = t < row->childCount() ? row->child(t) : nullptr;
            if (!tailItem) {
                tailItem = new QTreeWidgetItem(row);
                tailItem->setData(0, k_kindRole, static_cast<int>(ObjectController::Subject::Tail));
                tailItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);   // not draggable, not a drop target
            }
            tailItem->setData(0, Qt::UserRole, uid);
            tailItem->setData(0, k_tailRole, t);
            tailItem->setText(0, tr("Tail %1").arg(t + 1));
            if (m_tailGlyph.isNull())
                m_tailGlyph = tailGlyph(m_tree->palette(), k_rowGlyphPx, m_tree->devicePixelRatioF());
            tailItem->setIcon(0, m_tailGlyph);
            tailItem->setSelected(m_objects.selectedTails().contains(ObjectController::TailRef{uid, t}));
        }
    }

    // The strip, pinned last — under every object, because it is what they all sit on. It is not an
    // overlay: nothing can drag it, nothing can be dropped on it, and it has no mute, because a strip
    // that could be hidden would no longer show what renders. Absent until there is a layout.
    // Rows for overlays that are gone go *before* the strip is placed. Left in until the end they would
    // still occupy positions above it, and the strip would be moved — taken and re-inserted — on every
    // deletion, when all that had changed was a row above it disappearing.
    QTreeWidgetItem* strip = unused.take(k_stripId);
    qDeleteAll(unused);
    unused.clear();
    if (m_layout.isEmpty()) {
        delete strip;
        strip = nullptr;
    }

    if (!m_layout.isEmpty()) {
        if (!strip) {
            strip = new QTreeWidgetItem;
            strip->setData(0, Qt::UserRole, k_stripId);
            strip->setData(0, k_kindRole, static_cast<int>(ObjectController::Subject::Strip));
            strip->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        }
        placeTopLevel(strip, index);
        strip->setText(0, tr("Strip · %n page(s)", "", m_layout.pageCount()));
        if (m_stripGlyph.isNull())
            m_stripGlyph = stripGlyph(m_tree->palette(), k_rowGlyphPx, m_tree->devicePixelRatioF());
        strip->setIcon(0, m_stripGlyph);

        // Its pages, in strip order, reconciled the same way — by input uid, in place, so a page the
        // artist had selected is still selected after an edit that did not remove it.
        QHash<QString, QTreeWidgetItem*> oldPages;
        for (int c = 0; c < strip->childCount(); ++c)
            oldPages.insert(strip->child(c)->data(0, Qt::UserRole).toString(), strip->child(c));

        for (int i = 0; i < m_layout.pageCount(); ++i) {
            const Page& page = m_layout.page(i);
            QTreeWidgetItem* row = oldPages.take(page.inputUid);
            if (!row) {
                row = new QTreeWidgetItem;
                row->setData(0, Qt::UserRole, page.inputUid);
                row->setData(0, k_kindRole, static_cast<int>(ObjectController::Subject::Page));
                row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);   // not draggable, not a drop target
            }
            const int at = strip->indexOfChild(row);
            if (at != i) {
                if (at >= 0)
                    strip->takeChild(at);
                strip->insertChild(i, row);
            }
            const QString name = tr("p.%1 — %2").arg(i + 1, 2, 10, QLatin1Char('0'))
                                                .arg(QFileInfo(page.sourcePath).fileName());
            row->setText(0, m_excludedPages.contains(page.inputUid) ? tr("%1 · excluded").arg(name) : name);
            if (m_pageGlyph.isNull())
                m_pageGlyph = pageGlyph(m_tree->palette(), k_rowGlyphPx, m_tree->devicePixelRatioF());
            row->setIcon(0, m_pageGlyph);
            row->setSelected(m_objects.subject() == ObjectController::Subject::Page && page.inputUid == m_objects.selectedPage());
        }
        qDeleteAll(oldPages);   // pages no longer in the strip
        strip->setSelected(m_objects.subject() == ObjectController::Subject::Strip);
    }

    m_syncing = false;
}

void ObjectStack::showSelectedRows()
{
    m_syncing = true;
    m_tree->clearSelection();
    for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
        QTreeWidgetItem* row = m_tree->topLevelItem(r);
        if (row->data(0, k_kindRole).toInt() != static_cast<int>(ObjectController::Subject::Overlay))
            continue;
        const QString uid = row->data(0, Qt::UserRole).toString();
        // A tail's row stands for the tail; its balloon's row is only selected when the balloon itself is.
        row->setSelected(m_objects.selectedOverlays().contains(uid) && !m_objects.carriers().contains(uid));
        for (int c = 0; c < row->childCount(); ++c)
            row->child(c)->setSelected(m_objects.selectedTails().contains(ObjectController::TailRef{uid, c}));
    }
    m_syncing = false;
}

void ObjectStack::showSubjectRow()
{
    m_syncing = true;
    if (QTreeWidgetItem* row = subjectRow())
        row->setSelected(true);
    m_syncing = false;
}

void ObjectStack::revealSelectedRow()
{
    const QList<QTreeWidgetItem*> rows = m_tree->selectedItems();
    if (!rows.isEmpty())
        m_tree->scrollToItem(rows.first());
}

QTreeWidgetItem* ObjectStack::subjectRow() const
{
    for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
        QTreeWidgetItem* top = m_tree->topLevelItem(r);
        if (top->data(0, k_kindRole).toInt() != static_cast<int>(ObjectController::Subject::Strip))
            continue;
        if (m_objects.subject() == ObjectController::Subject::Strip)
            return top;
        for (int c = 0; c < top->childCount(); ++c)
            if (top->child(c)->data(0, Qt::UserRole).toString() == m_objects.selectedPage())
                return top->child(c);
    }
    return nullptr;
}

void ObjectStack::commitOrder()
{
    // First, before any return: a drag that ends in an early exit must not leave the tree ignoring
    // every selection made after it.
    m_rowsMoving = false;

    // The rows are the composite order reversed (see refresh()): row 0 is the front-most object and the
    // render draws last-on-top, so the controller is handed the rows read bottom-up.
    QStringList bottomUp;
    for (int r = m_tree->topLevelItemCount() - 1; r >= 0; --r)
        bottomUp.append(m_tree->topLevelItem(r)->data(0, Qt::UserRole).toString());
    m_objects.setStackOrder(bottomUp);
}

}  // namespace StripEdit
