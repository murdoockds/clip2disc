#ifndef CLICKOVERLAY_H
#define CLICKOVERLAY_H

#include <QWidget>

class QLabel;

class ClickOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ClickOverlay(QWidget *parent = nullptr);

    void showText(bool visible);
    void setText(const QString &text);
    void setClickable(bool clickable);
    QLabel *m_label;
signals:
    void clicked();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    bool m_clickable = true;
};

#endif // CLICKOVERLAY_H
