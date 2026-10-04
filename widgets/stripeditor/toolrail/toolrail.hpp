#ifndef STRIPEDIT_TOOLRAIL_HPP
#define STRIPEDIT_TOOLRAIL_HPP

#include <QObject>
#include <QString>

class QButtonGroup;
class QWidget;

namespace StripEdit {

class ColourPair;

/**
 * @brief The TOOL RAIL: a tile per row of tools(), and the colour pair under them.
 *
 * Builds both into the form's rail widget and keeps them there; what a picked tool *does* is the
 * Editor's (setTool()). Two rows rather than one flow: the tiles, and under them the colour pair. They
 * used to share the flow, which worked and read badly — the pair took its turn in the grid as though it
 * were a ninth tool, and it is furniture.
 */
class ToolRail : public QObject
{
    Q_OBJECT

public:
    //! Builds the tiles and the colour pair into @p host, the form's rail widget, which also parents this.
    explicit ToolRail(QWidget* host);

    //! The colour pair under the tiles. The tools that use it hold this pointer, never a colour.
    [[nodiscard]] ColourPair* colours() const { return m_colours; }

    //! Checks the tile of tool @p id, without emitting toolPicked().
    void setCurrent(const QString& id);

signals:
    void toolPicked(const QString& id);   //!< A tile was clicked.

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget*      m_tiles   = nullptr;
    QButtonGroup* m_group   = nullptr;   //!< A button's id is its row in tools().
    ColourPair*   m_colours = nullptr;
};

}  // namespace StripEdit

#endif // STRIPEDIT_TOOLRAIL_HPP
