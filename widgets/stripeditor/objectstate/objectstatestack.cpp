#include "objectstate/objectstatestack.hpp"

#include "objects/striplayout.hpp"
#include "objectstate/objectstate.hpp"
#include "objectstate/stripstate.hpp"
#include "scrolledpage.hpp"

#include <QFileInfo>
#include <QStackedWidget>

#include <algorithm>

namespace StripEdit {

namespace {

[[nodiscard]] bool isSkipped(const Platemaker::Models::ColourCorrection& cc, const QString& inputUid)
{
    const auto& skipped = cc.excludedInputUids;
    return std::find(skipped.begin(), skipped.end(), inputUid.toStdString()) != skipped.end();
}

} // namespace

ObjectStateStack::ObjectStateStack(QStackedWidget* stack, ObjectState* objects, StripState* strip)
    : QObject(stack)
    , m_stack(stack)
    , m_strip(strip)
{
    m_objectPage = scrolled(objects, stack);
    stack->addWidget(m_objectPage);
    m_stripPage = scrolled(strip, stack);
    stack->addWidget(m_stripPage);
}

void ObjectStateStack::showObject()
{
    // One object panel for every kind of object. There were two — a balloon's and a picture's — and
    // the second was the first with its sections hidden, which is what deciding which sections apply
    // already does. A picture's one extra question, how big it is drawn, is a row in the same panel.
    m_stack->setCurrentWidget(m_objectPage);
}

void ObjectStateStack::showStrip(const StripLayout& layout, const Platemaker::Models::ColourCorrection& cc)
{
    int excluded = 0;
    for (int i = 0; i < layout.pageCount(); ++i)
        if (isSkipped(cc, layout.page(i).inputUid))
            ++excluded;
    m_strip->showStrip(layout.pageCount(), excluded, cc);
    m_stack->setCurrentWidget(m_stripPage);
}

void ObjectStateStack::showPage(const StripLayout& layout, int index,
                                const Platemaker::Models::ColourCorrection& cc)
{
    const Page& page = layout.page(index);
    m_strip->showPage(page.inputUid,
                      tr("p.%1 — %2").arg(index + 1, 2, 10, QLatin1Char('0'))
                                     .arg(QFileInfo(page.sourcePath).fileName()),
                      page.size, isSkipped(cc, page.inputUid),
                      !Platemaker::Models::isNeutral(cc));
    m_stack->setCurrentWidget(m_stripPage);
}

}  // namespace StripEdit
