#ifndef STRIPEDIT_ARTWORKOPTIONSPANEL_H
#define STRIPEDIT_ARTWORKOPTIONSPANEL_H

#include <QString>
#include <QWidget>

class QLabel;

namespace StripEdit {

/**
 * @brief Options for the Artwork tool: **which picture the next placement puts down**.
 *
 * The counterpart of `ToolOptionsPanel` for the other create tool, and the same contract — it describes
 * an object that does not exist yet, so it edits nothing and emits nothing about any object.
 *
 * It exists because importing was reachable only from the object list's context menu, which is a place
 * nobody looks for *adding* something. A tool makes it a gesture: choose a picture once, then drag it
 * onto the strip as many times as the chapter needs it, which is how sound effects and hand-drawn
 * balloons are actually used.
 *
 * The chosen file is remembered in `QSettings` — a working preference that follows the artist rather
 * than the comic, exactly as the tool's other options are.
 */
class ArtworkOptionsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ArtworkOptionsPanel(QWidget* parent = nullptr);

    //! The file the next placement uses, or empty — in which case the placement asks for one.
    [[nodiscard]] QString artwork() const { return m_file; }

    /**
     * @brief Chooses @p file without a dialog — what a placement calls after asking for one itself.
     *
     * So the file a placement had to ask for becomes the tool's file, and the second placement does not
     * ask again.
     */
    void setArtwork(const QString& file);

    //! Opens the file dialog. @return whether a file was chosen.
    bool chooseArtwork();

signals:
    //! The picture changed. The editor passes it to the controller, which is what places it.
    void artworkChanged(const QString& file);

private:
    void refresh();   //!< Preview and name follow m_file; an unreadable file says so rather than lying.

protected:
    /**
     * @brief Starts a drag from the preview, carrying the file.
     *
     * **The picture is dragged out of the panel rather than drawn onto the strip**, because a drag on
     * the canvas would have to mean a size, and the size artwork wants is its own — a sound effect
     * reaching past the strip's edge is the point of it, not an accident to correct. The payload is a
     * file URL, so a picture dragged in from the file manager lands the same way.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPoint m_pressAt;   //!< Where a press on the preview landed; a drag starts once it has travelled.

    QLabel* m_preview = nullptr;
    QLabel* m_name    = nullptr;
    QString m_file;
};

}  // namespace StripEdit

#endif // STRIPEDIT_ARTWORKOPTIONSPANEL_H
