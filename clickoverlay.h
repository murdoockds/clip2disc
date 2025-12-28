#ifndef CLICKOVERLAY_H
#define CLICKOVERLAY_H

#include <QWidget>

class QLabel;

class ClickOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ClickOverlay(QWidget *parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // CLICKOVERLAY_H
