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
 * No widgets, so the rules are tests rather than comments.
 */

#include <QString>
#include <QStringList>

#include <platemaker/models/strip_overlay.hpp>

#include <vector>

#include "artifact.hpp"

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

/**
 * @brief Puts a copy of \p source at \p dest, unless an identical file is already there.
 *
 * @return False when the copy failed, or when a **different** file already has that name — which is
 *         never overwritten: it is somebody's.
 */
[[nodiscard]] bool copyUnlessIdentical(const QString& source, const QString& dest);

/**
 * @brief Brings every file these overlays need into \p overlaysDir, and points the overlays at the copies.
 *
 * What *Save As* runs so a workspace references only its own folder, and what restoring an undo step runs,
 * since a history taken before a Save As still names the old folder. Carried along:
 *  - each overlay's own file, when it lies outside \p overlaysDir;
 *  - **the picture behind a lettered picture** — named by its record (`Artifact::artwork`), beside the
 *    wrapper, and by no overlay path at all, so following `assetPath` alone would leave it behind and the
 *    object with nothing to re-letter.
 *
 * A name already taken by a different file: an overlay's own file is copied under its content hash instead,
 * as a new one would be named; a picture is not renamed (its record names it) and fails the collect.
 * A balloon whose file is missing is pointed into \p overlaysDir anyway — it is regenerated from its
 * record there, never in the old folder. `sha256` is untouched: the bytes are the same.
 *
 * **All or nothing:** every copy is made before any path changes, so on failure \p overlays is exactly as
 * it was. Copies already made are harmless — nothing references them, and the sweep at open collects them.
 *
 * @param failed Set to the file that could not be carried, when the result is false.
 * @return True when every file is in place (or there was nothing to do).
 */
[[nodiscard]] bool collectOverlayFiles(std::vector<Platemaker::Models::StripOverlay>& overlays,
                                       const ArtifactMap&                              records,
                                       const QString&                                  overlaysDir,
                                       QString*                                        failed = nullptr);

#endif // WORKSPACEFOLDER_HPP
