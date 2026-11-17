#ifndef STRIPEDIT_SELECTION_HPP
#define STRIPEDIT_SELECTION_HPP

// What is selected in the strip editor: objects, elements of them (a balloon's tails, or one of its words), the strip
// or a page.
// QtCore only, so the rules are tested without a scene; the controller asks it and draws what it says.

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace StripEdit {

//! One element of one object on the strip: its uid and the element's id in its composition.
struct ElementRef
{
    QString object;
    int     element = 0;

    [[nodiscard]] bool operator==(const ElementRef& o) const { return object == o.object && element == o.element; }
};

/**
 * \brief What a selection is, as OBJECT STATE and the tools see it.
 *
 * The strip and its pages are not overlays — nothing about them is placed, styled or composited — so they are
 * told apart here rather than squeezed into an overlay's uid.
 */
enum class Subject { None, Overlay, Strip, Page, Tail, Words };

/**
 * \brief A selection: objects in pick order, elements of them, or the strip or one page.
 *
 * Objects and elements gather together (they share a position, which a drag moves); the strip and a page are
 * one of a kind. An element's object is selected with it, as its **carrier**: the object carries the handles the
 * element is moved by, but nobody picked it, so it is not a subject (not counted, not deleted, its row plain).
 */
class SelectionModel
{
public:
    //! Whether @p object exists (@p element 0), or has the element @p element.
    using Exists = std::function<bool(const QString& object, int element)>;

    /**
     * \brief Selects @p objects and @p elements, in that pick order; what does not exist, or comes twice, drops
     *        out, and each element's object comes in as its carrier unless it is picked itself.
     */
    void select(const QStringList& objects, const QList<ElementRef>& elements, const Exists& exists);
    /**
     * \brief Selects @p words alone, one of several words of an object, which comes in as their carrier; nothing
     *        when they do not exist. Words are not gathered with anything: the Text section edits one at a time.
     */
    void selectWords(const ElementRef& words, const Exists& exists);
    void selectStrip();                      //!< The strip, and nothing else.
    void selectPage(const QString& page);    //!< One page (its input uid), and nothing else.

    //! Everything selected on the canvas, in pick order: the objects picked and the carriers.
    [[nodiscard]] const QStringList&       objects() const { return m_objects; }
    [[nodiscard]] const QList<ElementRef>& elements() const { return m_elements; }   //!< In pick order.
    [[nodiscard]] const QStringList&       carriers() const { return m_carriers; }   //!< See the class.
    //! The objects that were picked: objects() without the carriers. What selecting the same again starts from.
    [[nodiscard]] QStringList picked() const;
    //! The primary: the last of objects(); empty for none.
    [[nodiscard]] const QString& primary() const { return m_primary; }
    [[nodiscard]] Subject        subject() const { return m_subject; }
    [[nodiscard]] const QString& page() const { return m_page; }   //!< When subject() is Page.
    //! When subject() is Tail: the one element, alone on its object; otherwise none (element 0).
    [[nodiscard]] ElementRef single() const;
    //! When subject() is Words: those words; otherwise none (element 0).
    [[nodiscard]] const ElementRef& words() const { return m_words; }
    //! How many things were picked: objects that are not carriers, and elements.
    [[nodiscard]] int subjectCount() const;

private:
    QStringList       m_objects;
    QList<ElementRef> m_elements;
    QStringList       m_carriers;
    ElementRef        m_words;   //!< See words().
    QString           m_primary;
    QString           m_page;
    Subject           m_subject = Subject::None;
};

}  // namespace StripEdit

#endif // STRIPEDIT_SELECTION_HPP
