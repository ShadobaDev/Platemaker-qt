#include "dockattention.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QWidget>

namespace {

constexpr int     k_flashMs  = 1500;    //!< Hold + fade, in one animation.
constexpr qreal   k_borderPx = 6.0;     //!< Thick enough to read at the edge of a screen.
constexpr qreal   k_cornerPx = 4.0;     //!< Slight rounding, so it reads as a frame and not as a glitch.
const     QString k_name     = QStringLiteral("dockAttentionOverlay");

/**
 * @brief The flash: a click-through child covering its dock, drawing a border that fades out.
 *
 * A child widget rather than a stylesheet on the dock. A stylesheet set here would have to be merged
 * with whatever the dock already carries and then unset again, and its selectors would reach every
 * widget inside; a widget that paints one rectangle and deletes itself leaves nothing behind.
 */
class AttentionOverlay : public QWidget
{
public:
    explicit AttentionOverlay(QDockWidget* dock)
        : QWidget(dock)
        , m_dock(dock)
    {
        setObjectName(k_name);
        setAttribute(Qt::WA_TransparentForMouseEvents);   // the dock stays usable while it flashes
        setAttribute(Qt::WA_NoSystemBackground);          // paint the border only; the dock shows through
        setGeometry(dock->rect());
        m_dock->installEventFilter(this);
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
    //! Follow the dock's size — showing a hidden dock can resize it right after we are built.
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_dock && event->type() == QEvent::Resize)
            setGeometry(m_dock->rect());
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QColor colour = palette().color(QPalette::Highlight);
        colour.setAlphaF(m_opacity);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(colour, k_borderPx));
        p.setBrush(Qt::NoBrush);
        const qreal inset = k_borderPx / 2.0;   // a pen straddles its path; keep the stroke inside
        p.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), k_cornerPx, k_cornerPx);
    }

private:
    QDockWidget* m_dock;
    qreal        m_opacity = 1.0;
};

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

    // One flash at a time: holding Ctrl+Z would otherwise stack an overlay per step.
    delete dock->findChild<QWidget*>(k_name, Qt::FindDirectChildrenOnly);
    new AttentionOverlay(dock);   // parented to the dock, and deletes itself when the fade ends
}
