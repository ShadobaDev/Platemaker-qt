#include "objects/artworkobject.hpp"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "painter.hpp"
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

ArtworkObject::ArtworkObject(QString uid, const QString& picture, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
{
    loadPicture(picture);
    // A picture that cannot be read still gets a box: an object with no size cannot be selected, moved
    // or deleted — which would strand it on the strip with no way to remove it.
    m_box = m_artwork.isNull() ? QSizeF(80, 80) : QSizeF(m_artwork.size());
    refreshBounds();
}

void ArtworkObject::setPicture(const QString& picture)
{
    if (picture == m_picture)
        return;
    loadPicture(picture);
    update();   // the drawn size is the artist's and is deliberately left alone
}

void ArtworkObject::loadPicture(const QString& picture)
{
    m_picture = picture;
    m_artwork = loadArtwork(picture);
    describePicture(m_composition);

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

QString ArtworkObject::label() const
{
    // What a row should say about a picture: its words if it has been lettered, otherwise the file —
    // which is the only thing that distinguishes one picture from another in a list of them.
    const Words*  words = wordsOf(m_composition);
    const QString said  = words ? words->text.body.section(QLatin1Char('\n'), 0, 0).trimmed() : QString();
    if (!said.isEmpty())
        return said;
    const QString file = QFileInfo(m_picture).fileName();
    return file.isEmpty() ? tr("(imported artwork)") : file;
}

void ArtworkObject::setComposition(const Composition& c)
{
    Composition next = c;
    describePicture(next);   // whatever arrived, this object is still this picture at its own pixels
    if (m_composition == next)
        return;
    m_composition = next;
    update();
    emit chromeChanged();   // its lettering may now fit, or not
}

void ArtworkObject::describePicture(Composition& c) const
{
    c.balloon.reset();   // the picture is the background: one at most
    if (!c.picture) {
        Picture p;
        p.id      = c.nextId++;
        c.picture = p;
    }
    c.picture->file = QFileInfo(m_picture).fileName();
    if (c.words.isEmpty()) {   // the words that can go over it, so the lettering has somewhere to be written
        Words w;
        w.id = c.nextId++;
        c.words.append(w);
    }
    // **This object is the authority on how big the picture is**, not the composition it was handed: that
    // may carry a size guessed from a copy the importer could not read. An empty pixmap leaves the size
    // alone — there is nothing better to say.
    if (!m_artwork.isNull())
        resize(c, m_artwork.size());
}

void ArtworkObject::setBoxSize(QSizeF size, const QPointF&)
{
    // A picture has no handles to keep in place.
    if (size == m_box)
        return;
    m_box = size;
    refreshBounds();
}

void ArtworkObject::paintContent(QPainter& painter)
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

    // The words over it, if there are any. Laid out in the picture's own pixels and drawn through the same
    // scale the picture is, so they sit on it rather than beside it however the object is resized. This is
    // the preview of what the wrapper SVG will render.
    const Words* words = wordsOf(m_composition);
    const QSize  size  = sizeOf(m_composition);
    if (!words || size.isEmpty())
        return;
    const QPainterPath text = Geometry::textOutline(m_composition, *words);
    if (text.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(m_box.width() / size.width(), m_box.height() / size.height());
    Painter::paintLettering(painter, *words, text, Geometry::textShadowOf(*words, text));
    painter.restore();
}

}  // namespace StripEdit
