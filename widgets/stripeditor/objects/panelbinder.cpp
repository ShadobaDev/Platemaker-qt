#include "objects/panelbinder.hpp"

#include <utility>

namespace StripEdit {

PanelBinder::PanelBinder(const SelectionModel& selection, Host host, QObject* parent)
    : QObject(parent), m_selection(selection), m_host(std::move(host))
{
}

void PanelBinder::bind()
{
    const QStringList&       picked      = m_selection.objects();
    const QList<ElementRef>& pickedTails = m_selection.elements();
    const QStringList&       carriers    = m_selection.carriers();
    const bool               oneObject   = picked.size() == 1 && pickedTails.isEmpty();

    // **Which objects OBJECT STATE is about to be bound to, in the order its records go in.** Set here and
    // only here, because this is the one place that decides what the panel is showing. A branch that binds
    // something uneditable leaves it empty, so a signal arriving from a panel that cannot be edited writes
    // nothing rather than writing to whatever happens to be selected now.
    m_subjects.clear();

    if (picked.isEmpty()) {
        emit boundToNothing();
    } else if (m_selection.subject() == Subject::Tail) {
        m_subjects = QStringList{m_selection.primary()};   // the tail's balloon owns the record
        emit boundToTail(m_host.composition(m_selection.primary()).value_or(Composition()), m_host.tailIndex(m_selection.single()));
    } else if (!pickedTails.isEmpty() && carriers.size() == picked.size()) {
        // Tails and nothing else: what they share is a tail's own section. Their balloons are the panel's
        // subjects, in this order, so an edit writes each tail into its balloon (applyEdits()).
        m_subjects = carriers;
        QList<Composition>     balloons;
        QList<QPair<int, int>> tails;
        for (const QString& uid : carriers)
            balloons << m_host.composition(uid).value_or(Composition());
        for (const ElementRef& t : pickedTails)
            tails << qMakePair(int(carriers.indexOf(t.object)), m_host.tailIndex(t));
        emit boundToTails(balloons, tails);
    } else if (!pickedTails.isEmpty()) {
        emit boundToMixed(m_selection.subjectCount());
    } else if (oneObject) {
        m_subjects = QStringList{m_selection.primary()};
        emit boundToRecord(m_host.composition(m_selection.primary()).value_or(Composition()));
    } else {
        // **Whatever kinds are in it.** The panel sorts the compositions into roles: skin and line style are
        // bound to the objects with a silhouette and written back only to those, and the lettering to all of
        // them. So the union is what shows, and the write path takes whichever kind each object is.
        m_subjects = picked;
        QList<Composition> subjects;
        subjects.reserve(picked.size());
        for (const QString& uid : picked)
            subjects.append(m_host.composition(uid).value_or(Composition()));
        emit boundToRecords(subjects);
    }

    // Whatever the panel is showing, it is showing it for objects that all have a blend mode —
    // the one property every kind carries. A tail is the exception: it is part of a balloon, and
    // the balloon's composite is the balloon's.
    const bool blends = pickedTails.isEmpty() && !picked.isEmpty();
    emit blendBound(blends ? m_host.blend() : std::nullopt, blends);
    // The other thing that belongs to the overlay and not to the drawing. One picture on its own
    // has a size to set; anything else has no single answer, so the row is not there to mislead.
    showArtworkScale();
}

void PanelBinder::show(const QList<Composition>& all)
{
    if (all.size() == 1)
        emit boundToRecord(all.first());
    else
        emit boundToRecords(all);
}

void PanelBinder::applyEdits(const QList<Composition>& compositions, bool commit)
{
    // **The panel edits properties, not geometry.** Its copy was made when it was bound, and the size and
    // the tail tips belong to no property group — they move on the canvas, and since the lettering learnt
    // to grow its box, under the panel's feet while it types. Taken from the panel they would put back a
    // size the last keystroke outgrew and tips from before it moved. So they are taken from the object,
    // which is where they live.
    QList<Composition> merged = compositions;
    for (int i = 0; i < merged.size() && i < m_subjects.size(); ++i) {
        const std::optional<Composition> object = m_host.composition(m_subjects.at(i));
        if (!object)
            continue;
        const Composition& held = *object;
        resize(merged[i], sizeOf(held));
        if (!merged[i].balloon || !held.balloon)
            continue;
        // By position: a panel adds a tail at the end and removes none (Delete tail is the controller's).
        QList<BalloonTail>& tails  = merged[i].balloon->tails;
        const auto          shared = qMin(held.balloon->tails.size(), tails.size());
        for (qsizetype t = 0; t < shared; ++t)
            tails[t].tail.tip = held.balloon->tails.at(t).tail.tip;
    }
    m_host.apply(m_subjects, merged, commit);
}

}  // namespace StripEdit
