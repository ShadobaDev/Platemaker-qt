#include "objects/selection.hpp"

namespace StripEdit {

void SelectionModel::select(const QStringList& objects, const QList<ElementRef>& elements, const Exists& exists)
{
    // Built aside and then adopted: the arguments may be this selection's own lists.
    QStringList picked;
    for (const QString& uid : objects)
        if (exists(uid, 0) && !picked.contains(uid))
            picked.append(uid);

    QList<ElementRef> pickedElements;
    QStringList       carriers;
    for (const ElementRef& e : elements) {
        if (!exists(e.object, e.element) || pickedElements.contains(e))
            continue;
        pickedElements.append(e);
        if (!picked.contains(e.object)) {
            picked.append(e.object);
            carriers.append(e.object);
        }
    }

    m_objects  = picked;
    m_elements = pickedElements;
    m_carriers = carriers;
    m_words    = {};
    m_primary  = picked.isEmpty() ? QString() : picked.last();
    m_page.clear();
    // One element, on its own object, is a selected tail; anything else with an element in it is a set whose
    // only common property is where it sits.
    m_subject = picked.isEmpty()           ? Subject::None
              : single().element != 0      ? Subject::Tail
                                           : Subject::Overlay;
}

void SelectionModel::selectWords(const ElementRef& words, const Exists& exists)
{
    if (!exists(words.object, words.element)) {
        select({}, {}, exists);
        return;
    }
    select({}, {}, exists);
    m_objects  = {words.object};
    m_carriers = {words.object};
    m_primary  = words.object;
    m_words    = words;
    m_subject  = Subject::Words;
}

void SelectionModel::selectStrip()
{
    select({}, {}, [](const QString&, int) { return false; });
    m_subject = Subject::Strip;
}

void SelectionModel::selectPage(const QString& page)
{
    select({}, {}, [](const QString&, int) { return false; });
    m_subject = Subject::Page;
    m_page    = page;
}

QStringList SelectionModel::picked() const
{
    QStringList out = m_objects;
    for (const QString& carrier : m_carriers)
        out.removeAll(carrier);
    return out;
}

ElementRef SelectionModel::single() const
{
    if (m_elements.size() == 1 && m_objects.size() == 1 && m_objects.first() == m_elements.first().object)
        return m_elements.first();
    return {};
}

int SelectionModel::subjectCount() const
{
    return int(m_objects.size() - m_carriers.size() + m_elements.size()) + (m_words.element != 0 ? 1 : 0);
}

}  // namespace StripEdit
