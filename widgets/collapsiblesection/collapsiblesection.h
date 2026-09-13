#ifndef COLLAPSIBLESECTION_H
#define COLLAPSIBLESECTION_H

#include <QString>
#include <QWidget>

class QToolButton;
class QVBoxLayout;

/**
 * @brief A titled row that folds one widget away.
 *
 * Qt has no such thing: a checkable `QGroupBox` puts a checkbox in the title that means *enabled*, not
 * *expanded*, and reads as "turn this off" to anyone who has met one before. This is the other idiom —
 * a disclosure arrow — built out of a flat `QToolButton` and a content widget, so it wears the palette
 * like everything else and carries no colours of its own.
 *
 * Expansion is the artist's business: nothing here opens or closes a section on its own. Whoever owns
 * the sections decides what starts open and is free to remember it between sessions.
 */
class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    /**
     * @brief Puts \p content inside, taking it into this widget's layout.
     *
     * One content widget per section. Calling it again replaces the previous one, which is not the
     * expected use — a section is built once and then bound to different subjects.
     */
    void setContent(QWidget* content);

    void setTitle(const QString& title);

    [[nodiscard]] bool isExpanded() const;
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

private:
    QToolButton* m_header  = nullptr;
    QVBoxLayout* m_lay     = nullptr;
    QWidget*     m_content = nullptr;
};

#endif // COLLAPSIBLESECTION_H
