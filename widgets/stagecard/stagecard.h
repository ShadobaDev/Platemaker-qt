#ifndef STAGECARD_H
#define STAGECARD_H

#include <QString>
#include <QWidget>

class QLabel;
class QToolButton;
class QMouseEvent;
class QPaintEvent;

/**
 * @brief One step in the Project "Workflow" pipeline map (built by Project::refreshWorkflowMap).
 *
 * A short bar: title on the left, a state line on the right, and — for the optional steps — action
 * buttons. Fixed stages are informational and jump to their tab on click (clicked()). Optional stages
 * (colour correction / text & bubbles) render greyed with a dashed border until active; which buttons
 * they carry is set by Project via setActions():
 *   - **add**    → a green "+", revealed on hover — *activate this step in place* (no navigation),
 *   - **edit**   → "Edit" — open the strip editor (also emitted on a double-click),
 *   - **remove** → "−" — deactivate / clear the step.
 *
 * The card emits intent only (clicked / addRequested / editRequested / removeRequested); Project decides
 * what each means. The frame is drawn through a Qt Style Sheet so `:hover` is tracked correctly by Qt
 * itself (including over the child buttons, in both directions); its colours use `palette(...)` so it
 * stays theme-aware — the one deliberate literal is the green "+".
 */
class StageCard : public QWidget
{
    Q_OBJECT

public:
    enum class Kind { Fixed, Optional };

    explicit StageCard(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void setKind(Kind kind);
    void setActive(bool active);
    //! Which action buttons to show on the right: add = green "+", edit = "Edit", remove = "−".
    void setActions(bool add, bool edit, bool remove);

    [[nodiscard]] QSize sizeHint() const override; //!< Uniform bar; a touch smaller when it's a greyed placeholder.

signals:
    void clicked();          //!< Single left-click on the body (fixed stages jump; CC placeholder activates).
    void addRequested();     //!< The green "+" — activate this step in place (no navigation).
    void editRequested();    //!< "Edit" or a double-click — open the strip editor.
    void removeRequested();  //!< "−" — deactivate / clear this step.

protected:
    void paintEvent(QPaintEvent* event) override;        //!< Renders the style-sheet box (bg/border/radius/:hover).
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void restyle();  //!< Rebuilds the state-dependent style sheet and button visibility.
    [[nodiscard]] bool isDimmed() const { return m_kind == Kind::Optional && !m_active; }

    QLabel*      m_titleLabel    = nullptr;
    QLabel*      m_subtitleLabel = nullptr;
    QToolButton* m_addButton     = nullptr;
    QToolButton* m_editButton    = nullptr;
    QToolButton* m_removeButton  = nullptr;

    Kind m_kind       = Kind::Fixed;
    bool m_active     = true;
    bool m_wantAdd    = false;
    bool m_wantEdit   = false;
    bool m_wantRemove = false;
};

#endif // STAGECARD_H
