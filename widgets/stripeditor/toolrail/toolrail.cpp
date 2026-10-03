#include "toolrail/toolrail.hpp"

#include "flowlayout.hpp"
#include "toolrail/colourpair.hpp"
#include "toolrail/toolregistry.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QToolButton>
#include <QVBoxLayout>

namespace StripEdit {

ToolRail::ToolRail(QWidget* host)
    : QObject(host)
{
    auto* railRows = new QVBoxLayout(host);
    railRows->setContentsMargins(0, 0, 0, 0);
    railRows->setSpacing(0);
    m_tiles       = new QWidget(host);
    auto* railLay = new FlowLayout(m_tiles, 6, 4, 4); // margin, hSpacing, vSpacing — wraps to fit
    railRows->addWidget(m_tiles);
    m_tiles->installEventFilter(this);   // see eventFilter: the tiles keep their own minimum
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    // A button per row, in the table's order, its id that row's index.
    for (int i = 0; i < tools().size(); ++i) {
        const Tool& t = tools().at(i);
        auto* b = new QToolButton(m_tiles);
        if (!t.icon.isEmpty())
            b->setIcon(QIcon(t.icon));
        b->setIconSize(QSize(26, 26));
        b->setToolTip(toolTooltip(t));   // the name, and the same sentence TOOL OPTIONS shows
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setFixedSize(40, 40);       // square tile
        railLay->addWidget(b);
        m_group->addButton(b, i);
    }
    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) { emit toolPicked(tools().at(id).id); });

    // The colour pair is **furniture**, not a tool: it sits under the tiles and stays there whichever
    // tool is active, because the tools that use it — the eyedropper fills it, an applicator spends
    // it — hold a reference to it rather than a colour of their own.
    m_colours       = new ColourPair(host);
    auto* colourRow = new QHBoxLayout;
    colourRow->setContentsMargins(6, 2, 6, 6);
    colourRow->addWidget(m_colours);
    colourRow->addStretch(1);       // left, where the tiles start
    railRows->addLayout(colourRow);
    railRows->addStretch(1);        // both rows hug the top; the rest of the rail is empty space
}

void ToolRail::setCurrent(const QString& id)
{
    if (QAbstractButton* b = m_group->button(toolIndex(id)))
        b->setChecked(true);
}

bool ToolRail::eventFilter(QObject* watched, QEvent* event)
{
    // **The tools are always all visible.** A flow layout's minimum is one tile, so a splitter was free
    // to shorten the rail until the last row of tools was simply not drawn — and a tool you cannot see
    // is a tool you do not know you have. The rail's minimum height is therefore whatever its own
    // wrapping needs at its current width, recomputed whenever that width changes.
    if (watched == m_tiles && event->type() == QEvent::Resize) {
        if (QLayout* flow = m_tiles->layout()) {
            const int needed = flow->heightForWidth(m_tiles->width());
            if (needed > 0 && needed != m_tiles->minimumHeight())
                m_tiles->setMinimumHeight(needed);
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace StripEdit
