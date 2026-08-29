#ifndef PROFILEPICKERDIALOG_H
#define PROFILEPICKERDIALOG_H

#include <QColor>
#include <QDialog>
#include <QList>
#include <QString>

class QWidget;

namespace Ui {
class ProfilePickerDialog;
}

/**
 * @brief A type-agnostic cherry-pick dialog: a checkable list of rows with a per-row inspection panel.
 *
 * Deliberately knows nothing about CanvasProfile / OutputProfile — the caller turns each profile into
 * a Row (title + one-line summary + a ready-made read-only widget for inspection), and reads back which
 * rows the user ticked via checkedIndices(). This lets one dialog serve both import and export, and
 * both profile kinds, without duplication. The @c details widget is the "precise inspection" surface:
 * the caller builds it (grouped fields, colour swatches) so the panel mirrors the profile editor.
 */
class ProfilePickerDialog : public QDialog
{
    Q_OBJECT

public:
    //! A small coloured chip drawn after the title or summary (e.g. "margins", "already in library").
    //! Dark text is drawn on the given background, so pick a light-ish colour.
    struct Badge {
        QString text;
        QColor  colour;
    };

    //! One selectable entry. @c details is shown in the right-hand panel while the row is current;
    //! ownership of the widget passes to the dialog on setRows().
    struct Row {
        QString      title;             //!< Primary label (the profile name).
        QString      summary;           //!< One-line summary under the title (dimensions / format).
        QList<Badge> titleBadges;       //!< Chips shown after the title (e.g. "already in library").
        QList<Badge> summaryBadges;     //!< Chips shown after the summary (e.g. "margins").
        QWidget*     details = nullptr; //!< Read-only inspection widget; the dialog takes ownership.
    };

    explicit ProfilePickerDialog(QWidget *parent = nullptr);
    ~ProfilePickerDialog() override;

    //! Header line above the list (e.g. the source path being imported from). Empty hides it.
    void setIntro(const QString &text);

    //! Label for the accept button (e.g. "Import", "Export"). Defaults to "OK".
    void setConfirmText(const QString &text);

    //! Populates the list. @p checkAllByDefault ticks every row (the common case for a fresh source).
    void setRows(const QList<Row> &rows, bool checkAllByDefault = true);

    //! Indices (into the rows passed to setRows) the user left checked. Valid after exec() == Accepted.
    [[nodiscard]] QList<int> checkedIndices() const;

private slots:
    void onCurrentRowChanged(int row); //!< Shows the current row's details in the inspection pane.
    void onSelectAll();                //!< Ticks every row.
    void onSelectNone();               //!< Unticks every row.
    void updateConfirmEnabled();       //!< Enables the accept button only while at least one row is ticked.

private:
    Ui::ProfilePickerDialog *ui;
    QList<Row>               m_rows;
};

#endif // PROFILEPICKERDIALOG_H
