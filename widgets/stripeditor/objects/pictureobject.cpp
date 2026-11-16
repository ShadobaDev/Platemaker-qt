#include "objects/pictureobject.hpp"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "painter.hpp"
#include <QSvgRenderer>

#include <utility>

namespace StripEdit {

namespace {

//! The box of a picture whose file cannot be read: an object with no size could not be selected, moved or
//! deleted, which would strand it on the strip.
constexpr int k_unreadSide = 80;

} // namespace

QPixmap loadPixmap(const QString& path)
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

PictureObject::PictureObject(QString uid, const QString& picture, QGraphicsItem* parent)
    : Object(std::move(uid), parent)
{
    loadPicture(picture);
    refreshBounds();
}

void PictureObject::setPicture(const QString& picture)
{
    if (picture == m_picture)
        return;
    loadPicture(picture);
    refreshBounds();   // its width is the artist's and is kept; its height follows the new file's aspect
    update();
}

void PictureObject::loadPicture(const QString& picture)
{
    m_picture = picture;
    m_pixmap = loadPixmap(picture);
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

QString PictureObject::label() const
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

void PictureObject::setComposition(const Composition& c)
{
    Composition next = c;
    describePicture(next);   // whatever arrived, this object is still this picture, in its file's aspect
    if (m_composition == next)
        return;
    m_composition = next;
    refreshBounds();   // a resize, or its lettering, which may now fit or not
    update();
}

void PictureObject::describePicture(Composition& c) const
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
    for (Words& w : c.words)   // a picture's words sit on it, in a frame its handles move
        if (!seated(c, w))
            w.seat = c.picture->id;
    // **Its size is the artist's, its aspect the file's.** The composition says how big the picture is drawn, as
    // a body's says how big the body is, so a resize never scales its words; the file says its proportions, which
    // a size guessed from a copy the importer could not read may not have. With no size, the file's own pixels.
    QSize size = sizeOf(c);
    if (!m_pixmap.isNull()) {
        const QSize own = m_pixmap.size();
        size            = size.isEmpty() ? own : QSize(size.width(), qMax(1, qRound(qreal(size.width()) * own.height() / own.width())));
    } else if (size.isEmpty()) {
        size = QSize(k_unreadSide, k_unreadSide);
    }
    resize(c, size);
}

bool PictureObject::canEditShape() const
{
    const Words* words = wordsOf(m_composition);
    return words && !words->text.body.isEmpty();
}

QPainterPath PictureObject::editOutline() const
{
    QPainterPath box;
    box.addRect(QRectF(QPointF(0, 0), boxSize()));
    return box;
}

void PictureObject::setBoxSize(QSizeF size, const QPointF&)
{
    // As a balloon's: the composition's size, offered to the document. The width leads; the height is the file's
    // aspect (describePicture()).
    Composition next = m_composition;
    resize(next, QSize(qRound(size.width()), qRound(size.height())));
    describePicture(next);
    if (next != m_composition)
        emit compositionEdited(uid(), next);
}

void PictureObject::paintContent(QPainter& painter)
{
    const QRectF box(QPointF(0, 0), boxSize());
    if (m_svg) {
        // Vector in, vector out: the renderer draws at the transform the view is showing, so zooming
        // in sharpens it instead of magnifying pixels somebody chose at import time.
        painter.setRenderHint(QPainter::Antialiasing, true);
        m_svg->render(&painter, box);
    } else if (!m_pixmap.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(box, m_pixmap, QRectF(m_pixmap.rect()));
    } else {
        return;
    }

    // The words over it, if there are any, laid out in its box at their own size: the preview of what the
    // wrapper SVG will render.
    const Words* words = wordsOf(m_composition);
    if (!words)
        return;
    const QPainterPath text = Geometry::textOutline(m_composition, *words);
    if (text.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    Painter::paintLettering(painter, *words, text, Geometry::textShadowOf(*words, text));
    painter.restore();
}

}  // namespace StripEdit
