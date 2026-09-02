#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

#include <QLayout>
#include <QList>
#include <QRect>
#include <QStyle>

/**
 * @brief A layout that arranges its items left-to-right and wraps to a new row when it runs out of width
 *        — like a toolbox that fits as many buttons per row as the current width allows.
 *
 * The canonical Qt "Flow Layout" example (height-for-width). Used by the strip editor's tool rail so the
 * square tool tiles reflow into 1 / 2 / 3 … columns as the toolbox is resized.
 */
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int horizontalSpacing() const;
    [[nodiscard]] int verticalSpacing() const;
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    [[nodiscard]] QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

#endif // FLOWLAYOUT_H
