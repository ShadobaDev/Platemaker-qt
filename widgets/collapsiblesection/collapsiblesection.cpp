#include "collapsiblesection.hpp"

#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(0, 0, 0, 0);
    m_lay->setSpacing(2);

    m_header = new QToolButton(this);
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setChecked(false);
    m_header->setAutoRaise(true);                       // a header, not a button to press
    m_header->setArrowType(Qt::RightArrow);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_lay->addWidget(m_header);

    connect(m_header, &QToolButton::toggled, this, [this](bool on) {
        m_header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        if (m_content)
            m_content->setVisible(on);
        emit expandedChanged(on);
    });
}

void CollapsibleSection::setContent(QWidget* content)
{
    if (m_content) {
        m_lay->removeWidget(m_content);
        m_content->setParent(nullptr);
    }
    m_content = content;
    if (!m_content)
        return;

    m_content->setParent(this);
    // Indented, so a collapsed stack of headers reads as a list and an expanded one as a tree.
    auto* row = new QHBoxLayout;
    row->setContentsMargins(12, 0, 0, 0);
    row->addWidget(m_content);
    m_lay->addLayout(row);
    m_content->setVisible(m_header->isChecked());
}

void CollapsibleSection::setTitle(const QString& title)
{
    m_header->setText(title);
}

bool CollapsibleSection::isExpanded() const
{
    return m_header->isChecked();
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_header->setChecked(expanded);   // the toggled() handler does the rest
}
