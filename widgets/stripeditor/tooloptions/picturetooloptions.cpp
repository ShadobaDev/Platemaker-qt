#include "tooloptions/picturetooloptions.hpp"

#include <QApplication>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QMimeData>
#include <QMouseEvent>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#include "objects/pictureobject.hpp"

namespace StripEdit {

namespace {

//! Where the tool's picture is remembered. A working preference, so it follows the artist.
const auto k_pictureKey = QStringLiteral("stripEditor/artworkTool/file");

constexpr int k_previewPx = 96;   //!< Enough to recognise a sound effect; not enough to be a viewer.

//! What the file dialog offers. The same three the import has always taken.
[[nodiscard]] QString pictureFilter()
{
    return PictureToolOptions::tr("Artwork (*.svg *.png *.webp);;All files (*)");
}

}  // namespace

PictureToolOptions::PictureToolOptions(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(k_previewPx);
    m_preview->setCursor(Qt::OpenHandCursor);   // it is a thing to pick up, and says so
    m_preview->setToolTip(tr("Drag onto the strip to place this picture at its own size."));
    m_preview->installEventFilter(this);
    lay->addWidget(m_preview);

    m_name = new QLabel(this);
    m_name->setWordWrap(true);
    m_name->setAlignment(Qt::AlignCenter);
    m_name->setForegroundRole(QPalette::PlaceholderText);   // it names the file; it is not a heading
    lay->addWidget(m_name);

    auto* choose = new QPushButton(tr("Choose picture…"), this);
    choose->setToolTip(tr("The picture every placement puts down until you choose another."));
    connect(choose, &QPushButton::clicked, this, [this] { choosePicture(); });
    lay->addWidget(choose);
    lay->addStretch(1);

    // The last picture, if it is still there. A file that has since moved is not an error to report at
    // start-up — it is simply no longer the tool's picture, and the next placement will ask.
    const QString last = QSettings().value(k_pictureKey).toString();
    if (!last.isEmpty() && QFileInfo::exists(last))
        m_file = last;
    refresh();
}

void PictureToolOptions::setPicture(const QString& file)
{
    if (file == m_file)
        return;
    m_file = file;
    QSettings().setValue(k_pictureKey, m_file);
    refresh();
    emit pictureChanged(m_file);
}

bool PictureToolOptions::choosePicture()
{
    const QString start = m_file.isEmpty() ? QString() : QFileInfo(m_file).absolutePath();
    const QString file  = QFileDialog::getOpenFileName(this, tr("Choose picture"), start,
                                                       pictureFilter());
    if (file.isEmpty())
        return false;
    setPicture(file);
    return true;
}

bool PictureToolOptions::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_preview || m_file.isEmpty())
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        m_pressAt = static_cast<QMouseEvent*>(event)->pos();
        return false;   // the press is still the label's; only travel makes it a drag
    }
    if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!(me->buttons() & Qt::LeftButton)
            || (me->pos() - m_pressAt).manhattanLength() < QApplication::startDragDistance())
            return false;

        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(m_file)});   // a file URL: the file manager speaks it too

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        // The preview itself goes under the cursor, centred — so the drop lands where the picture was
        // seen to be, which is what placePictureAt() then does with the point.
        if (const QPixmap shown = m_preview->pixmap(); !shown.isNull()) {
            drag->setPixmap(shown);
            drag->setHotSpot(shown.rect().center());
        }
        drag->exec(Qt::CopyAction);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void PictureToolOptions::refresh()
{
    if (m_file.isEmpty()) {
        m_preview->setPixmap({});
        m_preview->setText(tr("No picture chosen.\nChoose one, then drag it onto the strip."));
        m_name->clear();
        return;
    }

    // Loaded by the same function the placed object is drawn from, so what the panel shows and what
    // lands on the strip cannot be two different readings of the file.
    const QPixmap art = loadPixmap(m_file);
    if (art.isNull()) {
        m_preview->setPixmap({});
        m_preview->setText(tr("This file could not be read as a picture."));
    } else {
        m_preview->setText(QString());
        m_preview->setPixmap(art.scaled(QSize(k_previewPx, k_previewPx), Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
    }
    m_name->setText(QFileInfo(m_file).fileName());
}

}  // namespace StripEdit
