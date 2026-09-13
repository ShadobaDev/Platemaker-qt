#ifndef ADVISORYBAR_H
#define ADVISORYBAR_H

#include <QPointer>
#include <QString>
#include <QWidget>

class Advisories;
class QHBoxLayout;

/**
 * @brief A strip of advisory chips for one project, kept in step with the registry by itself.
 *
 * The registry was always meant to have more than one subscriber; what was missing was a **surface**
 * any window could host. A floating strip editor is its own top-level window with no status bar, so
 * the advisories were invisible in exactly the window where the work they describe is done.
 *
 * Hosts it two ways:
 * - `MainWindow` puts one in the status bar through `addPermanentWidget()`. One widget rather than one
 *   per chip, because `QStatusBar` frames every item it is handed and a frame per chip is a vertical
 *   rule between each pair.
 * - The strip editor puts one along its own bottom edge, and turns it off (`setActive(false)`) while
 *   its dock is inside the main window — where the status bar is already saying the same thing.
 *
 * Shows nothing when there is nothing to say: an empty bar is a strip of chrome reporting that all is
 * well, which is not worth the pixels.
 */
class AdvisoryBar : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param registry What to subscribe to. Held as a `QPointer` — the registry belongs to the
     *                 application and a bar can be destroyed with its window at any time, but the
     *                 reverse also has to be survivable.
     */
    explicit AdvisoryBar(Advisories* registry, QWidget* parent = nullptr);

    //! Which project this bar speaks about. Application-wide advisories are shown whatever it is.
    void setProjectUid(const QString& projectUid);

    /**
     * @brief Whether this bar is wanted at all.
     *
     * A host whose window already shows these advisories somewhere else turns its bar off rather than
     * destroying it, so it can come back the moment that stops being true — a dock being dragged out
     * of the main window, for instance.
     */
    void setActive(bool active);

private:
    void rebuild();          //!< Re-lays the chips from the registry.
    void updateVisibility(); //!< Wanted, and with something to say.

    QPointer<Advisories> m_advisories;
    QString              m_projectUid;
    QHBoxLayout*         m_chips   = nullptr;
    bool                 m_active  = true;
    bool                 m_hasAny  = false;
};

#endif // ADVISORYBAR_H
