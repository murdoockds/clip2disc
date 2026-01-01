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
    QLabel *m_label;
signals:
    void clicked();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // CLICKOVERLAY_H
