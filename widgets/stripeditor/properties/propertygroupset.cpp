#include "propertygroupset.h"

#include <QRandomGenerator>

#include "shapeeditor.h"
#include "skineditor.h"
#include "styleeditor.h"
#include "texteditor.h"

namespace StripEdit {

PropertyGroupSet::PropertyGroupSet(QWidget* host)
    : m_shape(new ShapeEditor(host))
    , m_skin(new SkinEditor(host))
    , m_style(new StyleEditor(host))
    , m_text(new TextEditor(host))
{
}

QList<PropertyGroupEditor*> PropertyGroupSet::all() const
{
    return {m_shape, m_skin, m_style, m_text};
}

void PropertyGroupSet::bind(const TextArtifact& a) const
{
    for (PropertyGroupEditor* e : all())
        e->bindOne(a);
}

void PropertyGroupSet::collect(TextArtifact& a, const PropertyGroupEditor* tails) const
{
    m_shape->applyTo(a);
    m_skin->applyTo(a);
    m_style->applyTo(a);
    m_text->applyTo(a);
    if (tails)
        tails->applyTo(a);   // last: it reads the shape that was just written

    // A bubble authored before styles existed carries seed 0, and so would every other one — style a
    // page of them and they would all wear the same wobble. Give it one the first time it is styled.
    if (a.style.kind != TextArtifact::Style::Clean && a.styleSeed == 0)
        a.styleSeed = QRandomGenerator::global()->generate();
}

}  // namespace StripEdit
