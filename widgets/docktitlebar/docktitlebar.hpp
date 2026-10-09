#ifndef DOCKTITLEBAR_HPP
#define DOCKTITLEBAR_HPP

#include <QWidget>
#include <QIcon>
#include <QRect>
#include <QVector>

class QBoxLayout;
class QDockWidget;
class QLabel;
class QSpacerItem;
class QToolButton;

/**
 * @brief A custom title bar for a QDockWidget.
 *
 * A live title label plus minimise / maximise / close buttons styled after the native window controls
 * (small `SP_TitleBar*` glyphs, a slightly longer minimise dash, roomy spacing). Set it with
 * `QDockWidget::setTitleBarWidget()`; Qt hides it while the dock is tabified (the tab stands in for it),
 * so the buttons show only while the dock floats or is docked on its own. The bar's empty area still
 * propagates mouse events to the dock, so drag-to-dock keeps working.
 *
 * It handles **maximise ⇄ restore** (fill the screen) itself — that is identical for every dock. It
 * leaves **minimise** and **close** to the owner via signals, because their meaning is dock-specific
 * (dock/detach with or without tabify; hide vs destroy). Reused by the Workspace, project, strip and
 * Action docks; the owner wires each one's behaviour (see `MainWindow::installDockTitleBar`).
 *
 * A dock may also be made **collapsible** (`enableCollapse`): a first button, `>|`, folds the bar into a
 * narrow column — the buttons stack vertically, the title hides and the glyph turns to `<|`. The bar only
 * changes its own shape; what collapsing does to the dock's content is the owner's, via `collapseToggled`.
 * Collapsing is a docked-only state: while the dock floats it shows expanded and the button hides, but the
 * choice is kept, so docking it again folds it back.
 */
class DockTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit DockTitleBar(QDockWidget *dock, QWidget *parent = nullptr);

    void enableCollapse();                       //!< Adds the collapse ⇄ expand button, first in the row.
    void setCollapsed(bool collapsed);           //!< Sets the choice; the bar folds while the dock is docked.
    [[nodiscard]] bool isCollapsed() const { return m_collapsed; }   //!< The choice, kept while floating.

signals:
    void minimiseClicked();   //!< The owner decides dock ⇄ detach (and any tabify).
    void closeClicked();      //!< The owner decides hide vs destroy.
    //! Whether the dock now shows collapsed (the choice, and docked); the owner decides what its content does.
    void collapseToggled(bool collapsed);

private:
    void toggleMaximise();               //!< Fill the screen ⇄ restore the previous geometry (self-contained).
    void applyCollapsed();                       //!< Shapes the bar (glyph, tooltip, row ⇄ column): folded when chosen and docked.
    [[nodiscard]] QIcon dashIcon(int px) const;  //!< A minimise glyph a bit longer than the style's default dash.
    //! A triangle against a bar: `>|` (collapse towards the right edge) or, @p pointLeft, `<|` (expand).
    [[nodiscard]] QIcon collapseIcon(int px, bool pointLeft) const;

    //! A gap between the buttons, kept so the collapsed column can drop it (its length runs across).
    struct Gap { QSpacerItem *item; int length; };

    QDockWidget *m_dock;
    bool         m_maximised = false;    //!< True while filling the screen; reset when the float state changes.
    QRect        m_restoreGeom;          //!< Geometry to restore on un-maximise.
    QBoxLayout  *m_layout = nullptr;
    QLabel      *m_title  = nullptr;
    QToolButton *m_collapseButton = nullptr;   //!< Null unless enableCollapse() was called.
    bool         m_collapsed = false;
    QVector<Gap> m_gaps;
};

#endif // DOCKTITLEBAR_HPP
