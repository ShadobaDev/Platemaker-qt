#include "assetobject.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "artifactpainter.h"
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
    return tr("(imported artwork)");
}

void AssetObject::setArtifact(const TextArtifact& a)
{
    if (m_artifact == a)
        return;
    m_artifact = a;
    update();
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
