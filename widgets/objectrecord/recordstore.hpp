#ifndef RECORDSTORE_HPP
#define RECORDSTORE_HPP

#include <QHash>
#include <QString>

#include "objectrecord.hpp"

/**
 * @brief Every open project's authoring records, keyed by project uid.
 *
 * A **cache**, not a store: the records live in the overlays' own SVG files (see recordsvg.hpp), and
 * this holds the parsed form so a populate() does not re-read and re-parse the whole chapter. It is
 * repopulated from disk when a workspace is opened and written through whenever the editor commits.
 *
 * There is deliberately nothing to save here. An authoring sidecar would be a second copy of what the
 * asset already carries — one more file to keep in step, and one more thing to lose separately from the
 * artwork it describes.
 */
class RecordStore
{
public:
    //! `<workspace dir>/overlays` — where the SVG assets live; created on demand by ensureDir().
    [[nodiscard]] static QString overlaysDir(const QString& workspacePath);
    //! Creates the overlays directory if missing. Returns its path, or empty if it cannot be created.
    [[nodiscard]] static QString ensureOverlaysDir(const QString& workspacePath);

    void clear() { m_byProject.clear(); }

    [[nodiscard]] ObjectRecord::Map        records(const QString& projectUid) const;
    void                             setRecords(const QString& projectUid, ObjectRecord::Map map);

private:
    QHash<QString, ObjectRecord::Map> m_byProject;   //!< project uid → (overlay uid → record)
};

#endif // RECORDSTORE_HPP
