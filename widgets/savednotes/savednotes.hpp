#ifndef SAVEDNOTES_HPP
#define SAVEDNOTES_HPP

#include <QCoreApplication>
#include <QLabel>
#include <QSizePolicy>

#include <functional>
#include <utility>
#include <vector>

/**
 * \brief Where a profile handed to an editor comes from: one the workspace holds, or one not saved yet
 *        (a new profile, a duplicate). Only a saved one has a saved value to show or go back to.
 */
enum class ProfileSource { Saved, Unsaved };

/**
 * \brief The "saved: …" notes of a profile editor: under each field whose value differs from the saved
 *        profile's, a small note saying what the saved one is.
 *
 * The notes are labels of the dialog's .ui, one under each field, styled by the dialog's
 * `QLabel[savedNote="true"]` rule. A note is never hidden, only emptied, and is one line high with a width
 * that does not count, so the dialog keeps its size as notes come and go: QFormLayout drops the row of
 * a hidden widget even with retainSizeWhenHidden, and a long note would otherwise widen the dialog.
 *
 * Each note is given a way to put a profile into the words its field shows. A field differs when those
 * words differ for the edited profile and the saved one, so what is compared is what the artist sees:
 * a resolution shown with two decimals differs only when the two decimals do.
 */
template <class Profile>
class SavedNotes
{
public:
    using Words = std::function<QString(const Profile&)>;   //!< A profile as one field shows it.

    //! Makes @p note the note of the field @p words reads; empty until that field differs.
    void add(QLabel* note, Words words)
    {
        note->ensurePolished();   // the font of the dialog's stylesheet, which the height is measured in
        note->setFixedHeight(note->fontMetrics().height());
        note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        note->clear();
        m_notes.push_back({note, std::move(words)});
    }

    /**
     * \brief Fills the note of every field where @p edited differs from @p saved, empties the others.
     * \return Whether any field differs.
     */
    bool refresh(const Profile& saved, const Profile& edited)
    {
        bool any = false;
        for (auto& n : m_notes) {
            const QString was = n.words(saved);
            const bool differs = was != n.words(edited);
            n.note->setText(differs ? QCoreApplication::translate("SavedNotes", "saved: %1").arg(was) : QString());
            any = any || differs;
        }
        return any;
    }

    //! Empties every note (a profile not saved yet has nothing to compare with).
    void clearAll()
    {
        for (auto& n : m_notes)
            n.note->clear();
    }

private:
    struct Note {
        QLabel* note;
        Words   words;
    };

    std::vector<Note> m_notes;
};

#endif // SAVEDNOTES_HPP
