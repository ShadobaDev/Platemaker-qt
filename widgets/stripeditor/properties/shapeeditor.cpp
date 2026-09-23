#include "shapeeditor.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QEvent>
#include <QIcon>
#include <QToolButton>
#include <QVBoxLayout>

#include "artifactpainter.hpp"
#include "flowlayout.hpp"

namespace StripEdit {

namespace {

//! Shape tile, matching the editor's tool rail so the two grids read as one family.
constexpr int k_shapeTilePx    = 44;
//! Supersampling factor for a tile's preview — rendered big, scaled down, so the stroke stays smooth.
constexpr int k_shapeIconScale = 4;

} // namespace

bool shapeSpeaks(Artifact::Shape shape)
{
    // Someone is talking: a tail belongs. A caption, a banner or a scroll is narration — it has no
    // speaker to point at, so placing one should not sprout a tail the author then has to turn off.
    switch (shape) {
    case Artifact::Shape::Speech:
    case Artifact::Shape::Shout:
    case Artifact::Shape::Ellipse:
    case Artifact::Shape::Thought:
        return true;
    default:
        return false;
    }
}

QPixmap bubbleThumbnail(Artifact::Shape shape, const QColor& fill, const QColor& stroke,
                        const QColor& ink)
{
    Artifact a;
    a.shape.kind       = shape;
    a.box              = QSize(k_bubbleThumbW * k_shapeIconScale, k_bubbleThumbH * k_shapeIconScale);
    // The tile's own stroke, not the artifact's: a preset authored at 5 px on a 280 px balloon would be
    // a hairline here, and the icon is meant to say *which shape and what colours*, not how heavy.
    a.skin.strokeWidth = 2 * k_shapeIconScale;
    a.text.body        = QStringLiteral("Aa");
    a.text.pixelSize   = a.box.height() / 3;
    a.skin.fill        = fill;
    a.skin.stroke      = stroke;
    a.text.colour      = ink;
    if (shapeSpeaks(shape)) {
        Tail t;
        // A short tail: the tile is scaled to fit, so a long one would shrink the balloon itself and
        // leave the speaking shapes visibly smaller than the rest of the grid.
        t.tip         = QPointF(a.box.width() * 0.28, a.box.height() * 1.10);
        t.baseWidth   = a.box.width() * 0.18;
        a.tails.items = {t};
    }
    return QPixmap::fromImage(renderArtifact(a).scaled(QSize(k_bubbleThumbW, k_bubbleThumbH),
                                                       Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QPixmap shapeThumbnail(Artifact::Shape shape, const QPalette& pal)
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
    auto* tileLay  = new FlowLayout(tileHost, 0, 4, 4);
    lay->addWidget(tileHost);

    m_tiles = new QButtonGroup(this);
    m_tiles->setExclusive(true);
    // The tiles are the shape list, in the shape list's order — both from artifact.hpp, so this
    // picker and *Convert to ▸* cannot come to offer different things or call them different names.
    for (Artifact::Shape shape : shapeOrder()) {
        auto* b = new QToolButton(tileHost);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(shapeTitle(shape));
        b->setIconSize(QSize(k_bubbleThumbW, k_bubbleThumbH));
        b->setFixedSize(k_shapeTilePx, k_shapeTilePx);
        tileLay->addWidget(b);
        m_tiles->addButton(b, int(shape));
    }

    if (auto* first = m_tiles->button(int(Artifact::Shape::Speech)))
        first->setChecked(true);
    refreshTiles();

    connect(m_tiles, &QButtonGroup::idClicked, this, [this](int id) {
        m_values.kind = static_cast<Artifact::Shape>(id);
        emit edited();
    });
}

void ShapeEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    m_values = ShapeProperties::from(*subjects.first());
    // Checkable buttons in an exclusive group do not emit on setChecked(), so no blocker is needed.
    if (auto* tile = m_tiles->button(int(m_values.kind)))
        tile->setChecked(true);
}

void ShapeEditor::applyTo(Artifact& target) const
{
    m_values.applyTo(target);
}

void ShapeEditor::changeEvent(QEvent* e)
{
    PropertyGroupEditor::changeEvent(e);
    if (e->type() == QEvent::PaletteChange)
        refreshTiles();
}

void ShapeEditor::refreshTiles()
{
    if (!m_tiles)
        return;
    for (QAbstractButton* b : m_tiles->buttons())
        b->setIcon(QIcon(shapeThumbnail(static_cast<Artifact::Shape>(m_tiles->id(b)), palette())));
}

}  // namespace StripEdit
