#ifndef TEMPLATESDIALOG_H
#define TEMPLATESDIALOG_H

#include <QDialog>

#include <platemaker/models/workspace.hpp>

namespace Ui { class TemplatesDialog; }

/**
 * \brief Manage and (re)generate canvas-profile templates for one workspace.
 *
 * Operates on a live Workspace reference (like the Project widget). Template
 * PNGs are written into a "templates" subfolder beside the workspace; each
 * profile's CanvasProfile::templateInfo records the relative path, a canvas-only
 * fingerprint, and a timestamp so the dialog can show whether a template is still
 * up to date.
 */
class TemplatesDialog : public QDialog
{
    Q_OBJECT

public:
    enum class TemplateStatus { NotGenerated, UpToDate, Outdated, FileMissing };

    explicit TemplatesDialog(Platemaker::Models::Workspace& workspace,
                             const QString& workspaceDir,
                             QWidget* parent = nullptr);
    ~TemplatesDialog() override;

    // Generates the template for \p cp into <workspaceDir>/templates/, updating
    // cp.templateInfo on success. Returns false and fills \p err on failure
    // (cp is left untouched). Reused by MainWindow's quick-generate button.
    static bool generateTemplate(const Platemaker::Models::Workspace& ws,
                                 const QString& workspaceDir,
                                 Platemaker::Models::CanvasProfile& cp,
                                 QString& err);

    // Canvas-only staleness check for an already-generated template.
    static TemplateStatus statusOf(const Platemaker::Models::CanvasProfile& cp,
                                   const QString& workspaceDir);

signals:
    // Emitted whenever a template is generated or deleted (workspace mutated).
    void workspaceModified();

private slots:
    void onSelectionChanged();
    void onGenerateSelected();
    void onGenerateAll();
    void onOpenFolder();
    void onDeleteTemplate();

private:
    void rebuildTable();
    int  selectedRow() const;
    static QString statusText(TemplateStatus status);

    Ui::TemplatesDialog* ui;
    Platemaker::Models::Workspace& m_workspace;
    QString m_workspaceDir;
};

#endif // TEMPLATESDIALOG_H
