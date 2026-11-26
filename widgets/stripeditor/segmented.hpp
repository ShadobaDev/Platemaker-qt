#ifndef STRIPEDIT_SEGMENTED_HPP
#define STRIPEDIT_SEGMENTED_HPP

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>
#include <QWidget>

#include <initializer_list>

namespace StripEdit {

/**
 * \brief @p buttons laid side by side, touching, one pressed (@p group, exclusive): a segmented switch. The first is
 *        pressed. Returns the row's widget, the buttons' parent.
 */
inline QWidget* segmented(QWidget* parent, QButtonGroup* group, std::initializer_list<QToolButton*> buttons)
{
    auto* row    = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (QToolButton* b : buttons) {
        b->setParent(row);
        b->setCheckable(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(b);
        layout->addWidget(b);
    }
    (*buttons.begin())->setChecked(true);
    return row;
}

//! A button of a segmented switch: @p text, @p tip as its tooltip.
inline QToolButton* segment(const QString& text, const QString& tip)
{
    auto* b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    return b;
}

}  // namespace StripEdit

#endif // STRIPEDIT_SEGMENTED_HPP
