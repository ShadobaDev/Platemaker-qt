#ifndef DOCKTITLEBAR_H
#define DOCKTITLEBAR_H

#include <QWidget>
#include <QIcon>
#include <QRect>

class QDockWidget;

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
 */
class DockTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit DockTitleBar(QDockWidget *dock, QWidget *parent = nullptr);

signals:
    void minimiseClicked();   //!< The owner decides dock ⇄ detach (and any tabify).
    void closeClicked();      //!< The owner decides hide vs destroy.

private:
    void toggleMaximise();               //!< Fill the screen ⇄ restore the previous geometry (self-contained).
    [[nodiscard]] QIcon dashIcon(int px) const;  //!< A minimise glyph a bit longer than the style's default dash.

    QDockWidget *m_dock;
    bool         m_maximised = false;    //!< True while filling the screen; reset when the float state changes.
    QRect        m_restoreGeom;          //!< Geometry to restore on un-maximise.
};

#endif // DOCKTITLEBAR_H
