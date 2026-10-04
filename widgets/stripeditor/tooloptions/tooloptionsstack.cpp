#include "tooloptions/tooloptionsstack.hpp"

#include "scrolledpage.hpp"
#include "toolrail/toolregistry.hpp"

#include <QFont>
#include <QLabel>
#include <QPalette>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace StripEdit {

ToolOptionsStack::ToolOptionsStack(QStackedWidget* stack)
    : QObject(stack)
    , m_stack(stack)
{
    auto* hintPage = new QWidget(stack);
    auto* hintLay  = new QVBoxLayout(hintPage);
    m_title        = new QLabel(hintPage);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_hint = new QLabel(hintPage);
    m_hint->setWordWrap(true);
    m_hint->setForegroundRole(QPalette::PlaceholderText);   // a remark, not an instruction
    hintLay->addWidget(m_title);
    hintLay->addWidget(m_hint);
    hintLay->addStretch(1);
    addPage(QString(), hintPage);
}

void ToolOptionsStack::addPage(const QString& key, QWidget* page)
{
    m_pages.insert(key, m_stack->addWidget(scrolled(page, m_stack)));
}

void ToolOptionsStack::assertEveryToolHasAPage() const
{
    for (const Tool& t : tools())
        Q_ASSERT(m_pages.contains(t.optionsPage));
}

void ToolOptionsStack::show(const Tool& tool)
{
    m_stack->setCurrentIndex(m_pages.value(tool.optionsPage));
    // Filled whichever page is showing: the hint page is the one that displays it, and writing it
    // unconditionally means there is no state to get wrong when tools are switched quickly.
    m_title->setText(toolName(tool));
    m_hint->setText(toolHint(tool));
}

}  // namespace StripEdit
