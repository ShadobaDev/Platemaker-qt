#include "assetobject.hpp"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "artifactpainter.hpp"
#include <QSvgRenderer>

#include <utility>

namespace StripEdit {

QPixmap loadArtwork(const QString& path)
{
    if (path.isEmpty())
        return {};

    if (!path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        return QPixmap(path);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};
    const QSize size = renderer.defaultSize();
    if (size.isEmpty())
        return {};

    QImage img(size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    renderer.render(&p);
    p.end();
    return QPixmap::fromImage(img);
}

AssetObject::AssetObject(QString uid, const QString& picture, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
{
    loadPicture(picture);
    // A picture that cannot be read still gets a box: an object with no size cannot be selected, moved
    // or deleted — which would strand it on the strip with no way to remove it.
    m_box = m_artwork.isNull() ? QSizeF(80, 80) : QSizeF(m_artwork.size());
    refreshBounds();
}

void AssetObject::setPicture(const QString& picture)
{
    if (picture == m_picture)
        return;
    loadPicture(picture);
    update();   // the drawn size is the artist's and is deliberately left alone
}

void AssetObject::loadPicture(const QString& picture)
{
    m_picture = picture;
    m_artwork = loadArtwork(picture);
    describePicture(m_artifact);

    delete m_svg;
    m_svg = nullptr;
    if (picture.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        auto* renderer = new QSvgRenderer(picture, this);
        if (renderer->isValid())
            m_svg = renderer;
        else
            delete renderer;
    }
}

QString AssetObject::label() const
{
    // What a row should say about a picture: its words if it has been lettered, otherwise the file —
    // which is the only thing that distinguishes one picture from another in a list of them.
    const QString said = m_artifact.text.body.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (!said.isEmpty())
        return said;
    const QString file = QFileInfo(m_picture).fileName();
    return file.isEmpty() ? tr("(imported artwork)") : file;
}

void AssetObject::setArtifact(const Artifact& a)
{
    Artifact next = a;
    describePicture(next);   // whatever arrived, this object is still this picture at its own pixels
    if (m_artifact == next)
        return;
    m_artifact = next;
    update();
}

void AssetObject::describePicture(Artifact& a) const
{
    a.artwork    = QFileInfo(m_picture).fileName();
    a.shape.kind = Artifact::Shape::None;   // no silhouette of ours, said both ways
    // **This object is the authority on how big the picture is**, not the record it was handed: the
    // record may predate pictures having one, or carry a size guessed from a copy the importer could
    // not read. An empty pixmap leaves the box alone — there is nothing better to say.
    if (!m_artwork.isNull())
        a.box = m_artwork.size();
}

void AssetObject::setBoxSize(QSizeF size)
{
    if (size == m_box)
        return;
    m_box = size;
    refreshBounds();
}

void AssetObject::paintContent(QPainter& painter)
{
    const QRectF box(QPointF(0, 0), m_box);
    if (m_svg) {
        // Vector in, vector out: the renderer draws at the transform the view is showing, so zooming
        // in sharpens it instead of magnifying pixels somebody chose at import time.
        painter.setRenderHint(QPainter::Antialiasing, true);
        m_svg->render(&painter, box);
    } else if (!m_artwork.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(box, m_artwork, QRectF(m_artwork.rect()));
    } else {
        return;
    }

    // The words over it, if there are any. Laid out in the record's box — the picture's own pixels —
    // and drawn through the same scale the picture is, so they sit on it rather than beside it however
    // the object is resized. This is the preview of what the wrapper SVG will render.
    const QPainterPath text = artifactTextOutline(m_artifact);
    if (text.isEmpty() || m_artifact.box.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(m_box.width() / m_artifact.box.width(), m_box.height() / m_artifact.box.height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_artifact.text.colour);
    painter.drawPath(text);
    painter.restore();
}

}  // namespace StripEdit
