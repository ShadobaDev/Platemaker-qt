#include "propertygroupset.h"

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

    topUpStyleSeed(a);
}

}  // namespace StripEdit
