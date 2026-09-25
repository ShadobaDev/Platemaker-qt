#ifndef WORKSPACEFOLDER_HPP
#define WORKSPACEFOLDER_HPP

/**
 * \file workspacefolder.hpp
 * \brief The folder a workspace lives in, and the rule that it belongs to that workspace alone.
 *
 * Everything the GUI writes for a workspace goes beside its file — `overlays/`, `templates/`,
 * `.platemaker-cache/` — so a second workspace file in the same folder shares all of it without either
 * knowing. Cleaning up what one of them no longer uses would then delete what the other still does:
 * the failure Cubase documents for *Remove unused media* and Ableton for its unused-files report, and
 * the reason both answer with the same rule. **A folder holds at most one workspace.**
 *
 * QtCore only, so the rule is a test rather than a comment.
 */

#include <QString>
#include <QStringList>

//! What names a workspace file. A profile bundle (`*.platemaker.profiles.json`) and the retired authoring
//! sidecar (`*.platemaker.overlays.json`) end differently, so the pattern tells them apart by itself.
inline constexpr char k_workspaceFilePattern[] = "*.platemaker.json";

/**
 * @brief Every workspace file directly inside \p dir, as absolute paths in name order.
 *
 * Not recursive: a workspace in a subfolder owns that subfolder, which is exactly the arrangement the
 * rule asks for. An unreadable or missing folder holds no workspaces.
 */
[[nodiscard]] QStringList workspacesInFolder(const QString& dir);

/**
 * @brief The workspaces in \p workspacePath's folder other than that file itself.
 *
 * What New and Save As must refuse to join, and what makes a folder unsafe to open. Compared as files,
 * not as strings, so a path spelled with different case or separators is still the same workspace.
 */
[[nodiscard]] QStringList otherWorkspacesBeside(const QString& workspacePath);

#endif // WORKSPACEFOLDER_HPP
