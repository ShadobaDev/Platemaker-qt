#ifndef STRIPEDIT_ASSETSTATEPANEL_H
#define STRIPEDIT_ASSETSTATEPANEL_H

#include <QString>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace StripEdit {

/**
 * @brief What a selected piece of **imported artwork** is — ③, for the kind that is not ours.
 *
 * A separate panel rather than a mode of `ObjectStatePanel`, for the reason the whole V series exists:
 * they describe different kinds of object. A balloon's panel is its property groups; artwork has no
 * groups — somebody else drew it — and exactly one thing to decide, which is **how big it is drawn**.
 *
 * Before this existed the properties panel bound a *default balloon* to artwork and offered a shape, a
 * fill and a line style for a photograph, all of which were silently swallowed. Then it said, honestly,
 * that there was nothing to edit. This is the third answer: there is one thing, and here it is.
 *
 * The size is a **percentage of the artwork's own pixels**, not a width in strip points: 100% is one
 * image pixel per strip pixel, which is what "original size" means to whoever drew it. Above 100% it
 * may reach past the strip's edge, which is a legitimate thing to want and why the tool places it at
 * its own size rather than fitting it.
 */
class AssetStatePanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssetStatePanel(QWidget* parent = nullptr);

    //! Shows @p name at @p percent of its own size. Emits nothing.
    void showArtwork(const QString& name, double percent);

signals:
    //! Live — while the spin box moves. The editor resizes the object without a history step.
    void scaleChanged(double percent);
    //! Settled — one history step, named for what it did.
    void scaleCommitted(double percent);
    void deleteRequested();

private:
    QLabel*         m_name  = nullptr;
    QDoubleSpinBox* m_scale = nullptr;
    QPushButton*    m_delete = nullptr;
    bool            m_populating = false;   //!< Suppresses the signals while binding, as ③'s others do.
};

}  // namespace StripEdit

#endif // STRIPEDIT_ASSETSTATEPANEL_H
