#include "properties/taillisteditor.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "shapes.hpp"   // Geometry::shapeSpeaks()

namespace StripEdit {

TailListEditor::TailListEditor(QWidget* parent)
    : PropertyGroupEditor(parent)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);

    m_count = new QLabel(this);
    m_count->setToolTip(tr("Each tail is its own object: select one — in the object list, or by its "
                           "handle on the strip — to change its width or bend, or to delete it."));
    row->addWidget(m_count, 1);

    m_add = new QPushButton(tr("Add tail"), this);
    m_add->setToolTip(tr("For a sound with more than one source. Drag the new handle to aim it."));
    row->addWidget(m_add);

    connect(m_add, &QPushButton::clicked, this, [this] {
        Tail t;
        t.look = m_values.nextLook;   // the look picked last on this balloon
        if (m_values.items.isEmpty()) {
            t.tip = m_origin + firstTailTip(m_box);
        } else {
            // Opposite the last tail, so it is visible rather than stacked on it, and shaped like it —
            // a starting value only; the new tail is edited on its own from here.
            const Tail& last = m_values.items.last();
            t.baseWidth = last.baseWidth;
            t.bend      = last.bend;
            t.tip       = QPointF(2 * m_origin.x() + m_box.width() - last.tip.x(), last.tip.y());
        }
        m_values.items.append(t);
        refresh();
        emit changed();
    });

    refresh();
}

void TailListEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    const Composition& a = *subjects.first();
    m_values        = TailsProperties::from(a, m_body);
    const Body* own = m_body != 0 ? bodyOf(a, m_body) : nullptr;
    m_box           = own ? own->size : sizeOf(a);
    m_origin        = own ? own->place.pos : QPointF();
    m_shapeCanSpeak = a.balloon.has_value();
    refresh();
}

void TailListEditor::applyTo(Composition& target) const
{
    TailsProperties next = m_values;
    if (!target.balloon.has_value())
        next.items.clear();   // nothing for a tail to grow from
    next.applyTo(target, m_body);
}

void TailListEditor::shapeChanged(const ShapeProperties& shape)
{
    m_shapeCanSpeak = shape.kind != ShapeProperties::Kind::None;
    // Never dropped: a tail is aimed at a speaker, and a new outline does not move the speaker. A tile
    // that speaks gives one to a balloon with none, as its tile shows.
    if (Geometry::shapeSpeaks(shape) && m_values.items.isEmpty()) {
        Tail t;
        t.tip = firstTailTip(m_box);
        m_values.items.append(t);
    }
    refresh();
}

void TailListEditor::refresh()
{
    m_count->setText(m_values.items.isEmpty()
                         ? tr("No tail")
                         : tr("%n tail(s)", "", static_cast<int>(m_values.items.size())));
    m_add->setEnabled(m_shapeCanSpeak);
}

}  // namespace StripEdit
