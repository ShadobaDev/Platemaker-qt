#ifndef STRIPEDIT_TOOLOPTIONSSTACK_HPP
#define STRIPEDIT_TOOLOPTIONSSTACK_HPP

#include <QHash>
#include <QObject>
#include <QString>

class QLabel;
class QStackedWidget;
class QWidget;

namespace StripEdit {

struct Tool;

/**
 * @brief TOOL OPTIONS: one page per *options page key*, and the page a tool with no options shows.
 *
 * The tool's own settings, under the rail, in the place every drawing application puts them. One page
 * per *key*, not per tool: tools that author the same object name the same page (`Tool::optionsPage`),
 * so there is one set of controls and no chance of two drifting apart. The pages themselves are built
 * and wired by the Editor; this holds them, and knows which one a tool shows.
 *
 * **A tool with no options is not a tool with nothing to say.** The page used to be blank, and a blank
 * panel under an armed tool reads as *nothing is armed* — which is how the bucket gets picked by
 * accident, and the next click paints. A tool whose key is empty gets the hint page instead: the tool's
 * name and its one sentence, both from the registry row, so a new tool cannot arrive without them.
 */
class ToolOptionsStack : public QObject
{
public:
    //! Builds the hint page into @p stack, the form's options stack, which also parents this.
    explicit ToolOptionsStack(QStackedWidget* stack);

    //! Registers @p page under @p key, wrapped to scroll rather than widen the column (see scrolled()).
    void addPage(const QString& key, QWidget* page);

    /**
     * @brief Asserts that every row of tools() names a page that was registered.
     *
     * A row naming a page nobody registered would land on index 0 — the hint page — and look like a
     * tool that simply has no options, which is the hardest kind of typo to see. Called once, after the
     * last addPage().
     */
    void assertEveryToolHasAPage() const;

    //! Shows @p tool's page, and writes its name and sentence into the hint page whichever page shows.
    void show(const Tool& tool);

private:
    QStackedWidget*     m_stack = nullptr;
    QHash<QString, int> m_pages;            //!< Options page key → its index in the stack.
    QLabel*             m_title = nullptr;  //!< The tool's name, on the hint page.
    QLabel*             m_hint  = nullptr;  //!< What a press does, on the hint page.
};

}  // namespace StripEdit

#endif // STRIPEDIT_TOOLOPTIONSSTACK_HPP
