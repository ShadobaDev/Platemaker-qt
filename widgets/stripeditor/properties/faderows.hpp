#ifndef STRIPEDIT_FADEROWS_HPP
#define STRIPEDIT_FADEROWS_HPP

#include "properties/propertygroupeditor.hpp"   // showCombo(), showCheck()
#include "propertygroups.hpp"                   // Paint

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QStandardItemModel>

#include <array>
#include <cmath>
#include <functional>

namespace StripEdit {

/**
 * \brief The rows of a Paint's fade that are not on the strip: where its cross stands (the middle, a side or a corner
 *        of the box it is laid on), its shape (a rectangle, or ellipses) and the check box that turns it over. The
 *        cross itself is dragged on the strip, in shape editing (Geometry::fadeHandles()).
 *
 * One fade editor for what fades by the cross: a balloon's fill (PaintRows places it) and a mask (MaskEditor, which
 * offers the shape among its own, a line too). The panel contract as FieldRows keeps it: *Mixed* while a bound
 * selection disagrees, *touched* once the artist moves a control. Holds no QObject; connects through the owner, and
 * must not move once built.
 */
class FadeRows
{
public:
    //! The context the rows' strings are translated in.
    static constexpr char k_trContext[] = "StripEdit::FadeRows";

    /**
     * \param p          The Paint the rows write to: the owner's own copy, written on every edit.
     * \param edited     Called after an edit is in @p p: the owner emits changed() there.
     * \param committed  Called after a discrete choice (the check box): the owner emits committed().
     * \param withShape  Whether to offer the shape row (Rectangle or Ellipse); a mask offers its shapes itself.
     */
    void build(QWidget* owner, QFormLayout* form, Paint& p, const bool& populating, std::function<void()> edited,
               std::function<void()> committed, bool withShape = true)
    {
        m_anchor = new QComboBox(owner);
        m_anchor->addItem(QCoreApplication::translate(k_trContext, "Custom"));   // dragged elsewhere: no anchor
        // Shown, not picked: the cross is put elsewhere by dragging its centre.
        if (auto* model = qobject_cast<QStandardItemModel*>(m_anchor->model()))
            model->item(0)->setEnabled(false);
        for (const Anchor& a : k_anchors)
            m_anchor->addItem(QCoreApplication::translate(k_trContext, a.name), QPointF(a.x, a.y));
        m_anchor->setToolTip(QCoreApplication::translate(k_trContext,
            "Where the cross stands: the middle, a side or a corner, its arms reaching to the sides from there. "
            "Dragged on the strip in shape editing, it is Custom."));
        form->addRow(QCoreApplication::translate(k_trContext, "Cross at"), m_anchor);
        QObject::connect(m_anchor, &QComboBox::currentIndexChanged, owner, [this, &p, &populating, edited](int i) {
            const QVariant at = m_anchor->itemData(i);
            if (populating || !at.isValid())
                return;
            p.fadeCentreX   = at.toPointF().x();
            p.fadeCentreY   = at.toPointF().y();
            m_anchorTouched = true;
            m_mixedAnchor   = false;
            edited();
        });
        if (withShape) {
            m_round = new QComboBox(owner);
            m_round->addItem(QCoreApplication::translate(k_trContext, "Rectangle"), false);
            m_round->addItem(QCoreApplication::translate(k_trContext, "Ellipse"), true);
            m_round->setToolTip(QCoreApplication::translate(k_trContext,
                "The fade's shape, set by the cross on the strip in shape editing: each arm fading along its own "
                "side, the corners square, or ellipses through the arms' dots."));
            form->addRow(QCoreApplication::translate(k_trContext, "Fade"), m_round);
            QObject::connect(m_round, &QComboBox::currentIndexChanged, owner, [this, &p, &populating, edited](int i) {
                if (populating || i < 0)
                    return;
                p.fadeRound    = m_round->currentData().toBool();
                m_roundTouched = true;
                m_mixedRound   = false;
                edited();
            });
        }
        m_reversed = new QCheckBox(QCoreApplication::translate(k_trContext, "From the centre"), owner);
        m_reversed->setToolTip(QCoreApplication::translate(k_trContext,
            "Turns the fade over: gone at the cross's centre and whole past its ends. The cross is set on the strip, "
            "in shape editing."));
        form->addRow(QString(), m_reversed);
        QObject::connect(m_reversed, &QCheckBox::toggled, owner, [this, &p, &populating, edited, committed](bool on) {
            if (populating)
                return;
            m_reversed->setTristate(false);   // they have taken a position
            p.fadeReversed    = on;
            m_reversedTouched = true;
            m_mixedReversed   = false;
            edited();
            committed();   // a tick is discrete, like a dialog's choice
        });
    }

    //! Starts a bind: nothing mixed, nothing touched.
    void reset()
    {
        m_mixedRound = m_mixedReversed = m_mixedAnchor = false;
        m_roundTouched = m_reversedTouched = m_anchorTouched = false;
    }

    //! Marks each row on which @p a disagrees with @p b as *Mixed*.
    void compare(const Paint& a, const Paint& b)
    {
        m_mixedReversed = m_mixedReversed || a.fadeReversed != b.fadeReversed;
        m_mixedRound    = m_mixedRound || a.fadeRound != b.fadeRound;
        m_mixedAnchor   = m_mixedAnchor || a.fadeCentreX != b.fadeCentreX || a.fadeCentreY != b.fadeCentreY;
    }

    //! Shows @p p's fade, or *Mixed*. Call it inside the owner's populating guard.
    void show(const Paint& p)
    {
        if (m_round)
            showCombo(m_round, m_mixedRound, m_round->findData(p.fadeRound));
        showCheck(m_reversed, m_mixedReversed, p.fadeReversed);
        // The anchor the centre stands on, or Custom (the first entry).
        int at = 0;
        for (int i = 1; i < m_anchor->count(); ++i) {
            const QPointF a = m_anchor->itemData(i).toPointF();
            if (std::abs(a.x() - p.fadeCentreX) < k_onAnchor && std::abs(a.y() - p.fadeCentreY) < k_onAnchor)
                at = i;
        }
        showCombo(m_anchor, m_mixedAnchor, at);
    }

    void setVisible(QFormLayout* form, bool shown)
    {
        if (m_round)
            form->setRowVisible(m_round, shown);
        form->setRowVisible(m_reversed, shown);
        form->setRowVisible(m_anchor, shown);
    }

    //! Copies into @p t the rows the artist has touched, from @p v.
    void applyEdited(const Paint& v, Paint& t) const
    {
        if (m_reversedTouched) t.fadeReversed = v.fadeReversed;
        if (m_roundTouched)    t.fadeRound    = v.fadeRound;
        if (m_anchorTouched) {
            t.fadeCentreX = v.fadeCentreX;
            t.fadeCentreY = v.fadeCentreY;
        }
    }

private:
    //! A place the cross may stand, in % of the box's width and height (Paint::fadeCentreX, fadeCentreY).
    struct Anchor
    {
        const char* name;
        qreal       x, y;
    };
    static constexpr std::array<Anchor, 9> k_anchors{{
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Centre"), 50.0, 50.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Left"), 0.0, 50.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Right"), 100.0, 50.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Top"), 50.0, 0.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Bottom"), 50.0, 100.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Top left"), 0.0, 0.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Top right"), 100.0, 0.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Bottom left"), 0.0, 100.0},
        {QT_TRANSLATE_NOOP("StripEdit::FadeRows", "Bottom right"), 100.0, 100.0},
    }};
    //! How near an anchor, in %, the centre counts as standing on it: half the tenth the centre is kept to.
    static constexpr qreal k_onAnchor = 0.05;

    QComboBox* m_anchor   = nullptr;
    QComboBox* m_round    = nullptr;   //!< None where the owner offers the shape itself (withShape).
    QCheckBox* m_reversed = nullptr;
    bool m_mixedRound      = false;
    bool m_roundTouched    = false;
    bool m_mixedReversed   = false;
    bool m_reversedTouched = false;
    bool m_mixedAnchor     = false;
    bool m_anchorTouched   = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_FADEROWS_HPP
