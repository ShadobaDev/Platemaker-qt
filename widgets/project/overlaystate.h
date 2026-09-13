#ifndef OVERLAYSTATE_H
#define OVERLAYSTATE_H

#include <vector>

#include <platemaker/models/strip_overlay.hpp>

#include "textartifact.h"

/**
 * @brief A project's text & bubbles, whole — the strip editor's half of the document.
 *
 * The two halves travel together and are never split from each other: an overlay's record says *which*
 * file renders, its artifact says what that file contains, and restoring one without the other would
 * leave the strip showing text the render does not bake.
 *
 * Typed rather than serialised, unlike the project-scope snapshot next door. That one is JSON because
 * the library owns its format and hands it over as a string; this one is ours at both ends, so there is
 * nothing to be gained by turning it into text and back.
 */
struct OverlayState
{
    std::vector<Platemaker::Models::StripOverlay> overlays;
    ArtifactMap                                   artifacts;

    [[nodiscard]] bool operator==(const OverlayState& o) const
    {
        return overlays == o.overlays && artifacts == o.artifacts;
    }
    [[nodiscard]] bool operator!=(const OverlayState& o) const { return !(*this == o); }
};

#endif // OVERLAYSTATE_H
