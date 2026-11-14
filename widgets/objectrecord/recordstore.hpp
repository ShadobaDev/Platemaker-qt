#ifndef RECORDSTORE_HPP
#define RECORDSTORE_HPP

#include <QHash>
#include <QString>

#include "composition.hpp"

/**
 * \brief Every open project's compositions, keyed by project uid.
 *
 * A **cache**, not a store: a composition is its overlay's content, which the workspace keeps with the overlay
 * (`StripOverlay::content`), and this holds the parsed form so a populate() does not re-parse the whole
 * chapter. It is filled when a workspace is opened (Svg::readOverlays()) and written through whenever the
 * editor commits. There is nothing to save here.
 */
class RecordStore
{
public:
    //! `<workspace dir>/overlays` — where the SVG assets live; created on demand by ensureDir().
    [[nodiscard]] static QString overlaysDir(const QString& workspacePath);
    //! Creates the overlays directory if missing. Returns its path, or empty if it cannot be created.
    [[nodiscard]] static QString ensureOverlaysDir(const QString& workspacePath);

    void clear() { m_byProject.clear(); }

    [[nodiscard]] Composition::Map records(const QString& projectUid) const;
    void                           setRecords(const QString& projectUid, Composition::Map map);

private:
    QHash<QString, Composition::Map> m_byProject;   //!< project uid → (overlay uid → composition)
};

#endif // RECORDSTORE_HPP
