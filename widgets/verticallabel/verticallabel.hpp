#ifndef VERTICALLABEL_HPP
#define VERTICALLABEL_HPP

#include <QString>
#include <QWidget>

/**
 * @brief One line of text turned 90° clockwise, read top to bottom like a side-panel tab.
 *
 * Elided with "…" to the height it is given; the full text is the tooltip. QLabel cannot rotate, hence
 * this. Used by the collapsed Action panel.
 */
class VerticalLabel : public QWidget
{
    Q_OBJECT

public:
    explicit VerticalLabel(QWidget *parent = nullptr);

    void setText(const QString &text);
    [[nodiscard]] QString text() const { return m_text; }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
};

#endif // VERTICALLABEL_HPP
