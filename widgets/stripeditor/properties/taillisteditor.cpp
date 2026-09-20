#include "taillisteditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "shapeeditor.h"   // shapeSpeaks()

namespace StripEdit {

namespace {

//! Where a balloon's first tail points: down and a little left of centre, which is where a reader expects
//! a speech balloon to be speaking from.
[[nodiscard]] QPointF firstTailTip(QSize box)
{
    return {box.width() * 0.28, box.height() * 1.25};
}

}  // namespace

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
        if (m_values.items.isEmpty()) {
            t.tip = firstTailTip(m_box);
        } else {
            // Opposite the last tail, so it is visible rather than stacked on it, and shaped like it —
            // a starting value only; the new tail is edited on its own from here.
            const Tail& last = m_values.items.last();
            t.baseWidth = last.baseWidth;
            t.bend      = last.bend;
            t.tip       = QPointF(m_box.width() - last.tip.x(), last.tip.y());
        }
        m_values.items.append(t);
        refresh();
        emit edited();
    });

    refresh();
}

void TailListEditor::bind(const Subjects& subjects)
{
    if (subjects.isEmpty())
        return;
    const TextArtifact& a = *subjects.first();
    m_values        = TailsProperties::from(a);
    m_box           = a.box;
    m_shapeCanSpeak = a.hasSilhouette();
    refresh();
}

void TailListEditor::applyTo(TextArtifact& target) const
{
    TailsProperties next = m_values;
    if (!target.hasSilhouette())
        next.items.clear();   // nothing for a tail to grow from
    next.applyTo(target);
}

void TailListEditor::shapeChanged(TextArtifact::Shape kind)
{
    m_shapeCanSpeak = kind != TextArtifact::Shape::None;
    if (!shapeSpeaks(kind)) {
        m_values.items.clear();
    } else if (m_values.items.isEmpty()) {
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
