#include "assetobject.h"

#include <QImage>
#include <QPainter>
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

AssetObject::AssetObject(QString uid, QPixmap artwork, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
    , m_artwork(std::move(artwork))
    , m_box(m_artwork.isNull() ? QSizeF(80, 80) : QSizeF(m_artwork.size()))
{
    // A null pixmap still gets a box: the artwork may be unreadable, and an object with no size cannot
    // be selected, moved or deleted — which would strand it on the strip with no way to remove it.
    refreshBounds();
}

QString AssetObject::label() const
{
    return tr("(imported artwork)");
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
    if (m_artwork.isNull())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(QRectF(QPointF(0, 0), m_box), m_artwork, QRectF(m_artwork.rect()));
}

}  // namespace StripEdit
