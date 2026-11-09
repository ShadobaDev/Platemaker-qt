#include "dockattention.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

namespace {

constexpr int     k_flashMs  = 1500;    //!< Hold + fade, in one animation.
constexpr qreal   k_borderPx = 6.0;     //!< Thick enough to read at the edge of a screen.
constexpr qreal   k_rowBorderPx = 3.0;  //!< Round a row: thick enough to find, thin enough to leave its text readable.
constexpr qreal   k_cornerPx = 4.0;     //!< Slight rounding, so it reads as a frame and not as a glitch.
const     QString k_name     = QStringLiteral("dockAttentionOverlay");

/**
 * \brief The flash: a click-through child covering its host, drawing a border that fades out — round the
 *        whole host, or round the part of it @p area names (a row), asked again on every frame.
 *
 * A child widget rather than a stylesheet on the host. A stylesheet set here would have to be merged
 * with whatever the host already carries and then unset again, and its selectors would reach every
 * widget inside; a widget that paints one rectangle and deletes itself leaves nothing behind.
 */
class AttentionOverlay : public QWidget
{
public:
    AttentionOverlay(QWidget* host, qreal borderPx, std::function<QRect()> area = {})
        : QWidget(host)
        , m_host(host)
        , m_borderPx(borderPx)
        , m_area(std::move(area))
    {
        setObjectName(k_name);
        setAttribute(Qt::WA_TransparentForMouseEvents);   // the host stays usable while it flashes
        setAttribute(Qt::WA_NoSystemBackground);          // paint the border only; the host shows through
        setGeometry(host->rect());
        m_host->installEventFilter(this);
        raise();
        show();

        auto* fade = new QVariantAnimation(this);
        fade->setDuration(k_flashMs);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        // Full strength for most of the run, then out quickly. A border that dims from the first frame
        // reads as a rendering artefact; one that holds and then goes reads as something pointing.
        fade->setEasingCurve(QEasingCurve::InQuart);
        connect(fade, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_opacity = v.toReal();
            update();
        });
        connect(fade, &QVariantAnimation::finished, this, &QObject::deleteLater);
        fade->start();
    }

protected:
    //! Follow the host's size — showing a hidden dock can resize it right after we are built.
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_host && event->type() == QEvent::Resize)
            setGeometry(m_host->rect());
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QColor colour = palette().color(QPalette::Highlight);
        colour.setAlphaF(m_opacity);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(colour, m_borderPx));
        p.setBrush(Qt::NoBrush);
        const qreal inset = m_borderPx / 2.0;   // a pen straddles its path; keep the stroke inside
        const QRectF frame = m_area ? QRectF(m_area()) : QRectF(rect());
        p.drawRoundedRect(frame.adjusted(inset, inset, -inset, -inset), k_cornerPx, k_cornerPx);
    }

private:
    QWidget*               m_host;
    qreal                  m_borderPx;
    std::function<QRect()> m_area;      //!< Empty: the whole host.
    qreal                  m_opacity = 1.0;
};

//! One flash at a time on @p host: holding Ctrl+Z would otherwise stack an overlay per step.
void replaceAttention(QWidget* host, qreal borderPx, std::function<QRect()> area = {})
{
    delete host->findChild<QWidget*>(k_name, Qt::FindDirectChildrenOnly);
    new AttentionOverlay(host, borderPx, std::move(area));   // parented to the host; deletes itself
}

} // namespace

void showDockAttention(QDockWidget* dock)
{
    if (!dock)
        return;

    // Asked before the raise — afterwards this dock is the focused one either way, and the answer we
    // want is "was the user already looking at it".
    const bool wasFocused = dock->isVisible() && dock->isAncestorOf(QApplication::focusWidget());

    dock->show();
    dock->raise();
    if (dock->isFloating())
        dock->activateWindow();
    if (QWidget* content = dock->widget())
        content->setFocus(Qt::OtherFocusReason);

    if (wasFocused)
        return;

    replaceAttention(dock, k_borderPx);
}

void showRowAttention(QAbstractItemView* view, int row)
{
    if (!view || !view->model() || row < 0 || row >= view->model()->rowCount())
        return;

    const QModelIndex first = view->model()->index(row, 0);
    view->setCurrentIndex(first);
    view->scrollTo(first);
    const QPersistentModelIndex last = view->model()->index(row, view->model()->columnCount() - 1);
    const QPersistentModelIndex at   = first;
    replaceAttention(view->viewport(), k_rowBorderPx, [view, at, last] {
        return at.isValid() ? view->visualRect(at).united(view->visualRect(last)) : QRect();
    });
}

void showPartAttention(QWidget* part)
{
    if (part)
        replaceAttention(part, k_rowBorderPx);
}
