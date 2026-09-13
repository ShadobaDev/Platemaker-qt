#ifndef STRIPEDIT_PROPERTYGROUPSET_H
#define STRIPEDIT_PROPERTYGROUPSET_H

#include <QList>
#include <QObject>

#include "propertygroupeditor.h"

namespace StripEdit {

class ShapeEditor;
class SkinEditor;
class StyleEditor;
class TailsEditor;
class TextEditor;

/**
 * @brief The five group editors, and the two rules about applying them all.
 *
 * Both surfaces that show a balloon's properties own one of these: the panel describing the selected
 * object, and the panel describing what the next object will be. Neither lays the editors out the same
 * way, and neither should — but *collecting* them must be identical, because two rules live there and
 * both are the kind that drifts when written out twice:
 *
 * - **Shape is applied before Tails**, since the tails editor reads the shape to decide whether a tail
 *   is possible at all. Nothing else in the five can collide: no property has two owners.
 * - **The style seed is topped up afterwards.** It belongs to no group precisely so that no editor and
 *   no preset can copy it, which leaves exactly one place responsible for minting one when a bubble
 *   first becomes styled.
 *
 * Not a widget. It owns the editors as children of whatever widget hosts them, and hands them out for
 * placement; where they go is the surface's business, and none of them is ever told which surface it
 * ended up in.
 */
class PropertyGroupSet
{
public:
    //! Builds the five, parented to \p host — which is where they will be laid out.
    explicit PropertyGroupSet(QWidget* host);

    [[nodiscard]] ShapeEditor* shape() const { return m_shape; }
    [[nodiscard]] SkinEditor*  skin()  const { return m_skin; }
    [[nodiscard]] StyleEditor* style() const { return m_style; }
    [[nodiscard]] TextEditor*  text()  const { return m_text; }
    [[nodiscard]] TailsEditor* tails() const { return m_tails; }

    //! All five, in the order a panel should show them.
    [[nodiscard]] QList<PropertyGroupEditor*> all() const;

    //! Shows \p a in every editor. Emits nothing, by the editors' own contract.
    void bind(const TextArtifact& a) const;

    //! Writes every group into \p a, in the one order that matters, and tops up the seed.
    void collect(TextArtifact& a) const;

private:
    ShapeEditor* m_shape = nullptr;
    SkinEditor*  m_skin  = nullptr;
    StyleEditor* m_style = nullptr;
    TextEditor*  m_text  = nullptr;
    TailsEditor* m_tails = nullptr;
};

}  // namespace StripEdit

#endif // STRIPEDIT_PROPERTYGROUPSET_H
