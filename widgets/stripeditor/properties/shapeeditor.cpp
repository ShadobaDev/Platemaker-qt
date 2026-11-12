#include "properties/shapeeditor.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QIcon>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include "recordpainter.hpp"
#include "shapes.hpp"
#include "flowlayout.hpp"
#include "properties/regularpolygondialog.hpp"

namespace StripEdit {

namespace {

//! Shape tile, matching the editor's tool rail so the two grids read as one family.
constexpr int k_shapeTilePx    = 44;
//! Supersampling factor for a tile's preview — rendered big, scaled down, so the stroke stays smooth.
constexpr int k_shapeIconScale = 4;

} // namespace

QPixmap bubbleThumbnail(const ShapeProperties& shape, const QColor& fill, const QColor& stroke,
                        const QColor& ink)
{
    ObjectRecord a;
    a.shape            = shape;
    a.box              = QSize(k_bubbleThumbW * k_shapeIconScale, k_bubbleThumbH * k_shapeIconScale);
    // The tile's own stroke, not the record's: a preset authored at 5 px on a 280 px balloon would be
    // a hairline here, and the icon is meant to say *which shape and what colours*, not how heavy.
    a.style.strokeWidth = 2 * k_shapeIconScale;
    a.text.body        = QStringLiteral("Aa");
    a.text.pixelSize   = a.box.height() / 3;
    a.skin.fill.colour        = fill;
    a.style.stroke.colour      = stroke;
    a.text.fill.colour = ink;
    if (Geometry::shapeSpeaks(shape)) {
        Tail t;
        // A short tail: the tile is scaled to fit, so a long one would shrink the balloon itself and
        // leave the speaking shapes visibly smaller than the rest of the grid.
        t.tip         = QPointF(a.box.width() * 0.28, a.box.height() * 1.10);
        t.baseWidth   = a.box.width() * 0.18;
        a.tails.items = {t};
    }
    return QPixmap::fromImage(Painter::render(a).scaled(QSize(k_bubbleThumbW, k_bubbleThumbH),
                                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QPixmap shapeThumbnail(const ShapeProperties& shape, const QPalette& pal)
{
    return bubbleThumbnail(shape, pal.color(QPalette::Base), pal.color(QPalette::WindowText),
                           pal.color(QPalette::WindowText));
}

// ---------------------------------------------------------------------------

ShapeEditor::ShapeEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // A reflowing grid of preview tiles, built the same way the editor's tool rail is (a flow layout
    // cannot be expressed in a .ui).
    auto* tileHost = new QWidget(this);
    m_tileHost     = tileHost;
    auto* tileLay  = new FlowLayout(tileHost, 0, 4, 4);
    lay->addWidget(tileHost);

    m_tiles = new QButtonGroup(this);
    // The tiles are the templates, in their order (shapes.hpp); a tile's id is its template's index.
    const QList<Geometry::ShapeTemplate>& templates = Geometry::shapeTemplates();
    for (int i = 0; i < templates.size(); ++i) {
        auto* b = new QToolButton(tileHost);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(QObject::tr(templates[i].title));
        b->setIconSize(QSize(k_bubbleThumbW, k_bubbleThumbH));
        b->setFixedSize(k_shapeTilePx, k_shapeTilePx);
        tileLay->addWidget(b);
        m_tiles->addButton(b, i);
    }
    refreshTiles();

    // The parameters on a widget of their own, which the panel lays out apart from the tiles.
    m_parameters = new QWidget(this);
    m_form       = new QFormLayout(m_parameters);
    m_form->setContentsMargins(0, 0, 0, 0);
    const auto edited = [this] {
        syncFromValues();   // off its template, the tile is no longer current
        emit changed();
    };
    m_sections.build(this, m_form, m_values, m_populating, edited);
    m_regular = new QPushButton(tr("Regular polygon / star…"), this);
    m_regular->setToolTip(tr("Replaces the points with a regular polygon or a star, irregular if you like. "
                             "They stay free to move afterwards."));
    m_form->addRow(QString(), m_regular);
    connect(m_regular, &QPushButton::clicked, this, &ShapeEditor::makeRegular);

    // The edge: laid over whichever outline.
    m_edge = new QComboBox(this);
    for (int i = 0; i <= int(ShapeProperties::k_lastEdge); ++i)
        m_edge->addItem(edgeTitle(static_cast<ObjectRecord::Edge>(i)), i);
    m_edge->setToolTip(tr("What is done to the outline's edge, pushed in along it: straight teeth, soft "
                          "waves, arcs bowing in, or narrow notches. The tails stay smooth, and the edge stays "
                          "when another shape is picked."));
    m_form->addRow(tr("Edge:"), m_edge);
    const Fields::Section& teeth = EdgeParams::section();
    m_edgeRows.build(this, m_form, "", teeth.rows, teeth.rowCount, Fields::k_trContext, m_values.teeth, m_populating,
                     edited);
    connect(m_edge, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_populating || i < 0)
            return;
        m_values.edge = static_cast<ObjectRecord::Edge>(m_edge->currentData().toInt());
        syncFromValues();
        emit changed();
    });

    connect(m_tiles, &QButtonGroup::idClicked, this, [this](int id) {
        // The whole template: its kind and every parameter, so a tile gives exactly what it shows — but
        // the edge, which is laid over any outline and stays.
        m_values = Geometry::withEdgeOf(Geometry::shapeTemplates().at(id).shape, m_values);
        syncFromValues();
        emit picked();
        emit changed();
    });

    syncFromValues();
}

void ShapeEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = ShapeProperties::from(*subjects.first());
    m_sections.reset();
    m_edgeRows.reset();
    // No higher than the balloon can show: past it the outline stops changing (Shape is one subject only).
    m_sections.setMaximum("cornerRadius", qFloor(Geometry::cornerRadiusLimit(subjects.first()->box)));
    syncFromValues();
}

void ShapeEditor::syncFromValues()
{
    m_populating = true;
    const Geometry::ShapeTemplate* current = Geometry::templateOf(m_values);
    const int                      id      = current ? int(current - Geometry::shapeTemplates().constData()) : -1;
    // An exclusive group cannot show no tile checked, so it is released while one is unchecked. Checkable
    // buttons do not emit idClicked() on setChecked(), so nothing here reads as a pick.
    m_tiles->setExclusive(false);
    for (QAbstractButton* b : m_tiles->buttons())
        b->setChecked(m_tiles->id(b) == id);
    m_tiles->setExclusive(true);

    m_sections.show(m_values);
    m_sections.showSections(m_form, true, false, Geometry::shapeInfo(m_values.kind).sections);
    m_form->setRowVisible(m_regular, m_values.kind == ShapeProperties::Kind::Polygon);
    {
        const QSignalBlocker block(m_edge);
        m_edge->setCurrentIndex(m_edge->findData(int(m_values.edge)));
    }
    m_edgeRows.show(m_values.teeth);
    m_edgeRows.setVisible(m_form, Geometry::edgeInfo(m_values.edge).modulate != nullptr);
    m_populating = false;
}

void ShapeEditor::makeRegular()
{
    // The balloon shows each set of nodes as it is made; OK keeps the last as one history step, Cancel puts
    // back what was there.
    const ShapeProperties before = m_values;
    RegularPolygonDialog  dialog(this);
    const auto show = [this](const QList<ShapeNode>& nodes) {
        m_values.nodes = nodes;
        syncFromValues();
        emit changed();
    };
    connect(&dialog, &RegularPolygonDialog::nodesChanged, this, show);
    show(dialog.nodes());
    if (dialog.exec() == QDialog::Accepted) {
        emit committed();
        return;
    }
    m_values = before;
    syncFromValues();
    emit changed();
}

void ShapeEditor::applyTo(ObjectRecord& target) const
{
    m_values.applyTo(target);
}

void ShapeEditor::changeEvent(QEvent* e)
{
    PropertyGroupEditor::changeEvent(e);
    if (e->type() == QEvent::PaletteChange)
        refreshTiles();
}

void ShapeEditor::updateRows()
{
    m_tileHost->setVisible(shows(Row::ShapeTiles));
    m_parameters->setVisible(shows(Row::ShapeParameters));
}

void ShapeEditor::refreshTiles()
{
    if (!m_tiles)
        return;
    for (QAbstractButton* b : m_tiles->buttons())
        b->setIcon(QIcon(shapeThumbnail(Geometry::shapeTemplates().at(m_tiles->id(b)).shape, palette())));
}

}  // namespace StripEdit
