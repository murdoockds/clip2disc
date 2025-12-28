#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QWidget>

class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setDuration(qint64 durationMs);
    void setPlayPosition(qint64 positionMs);

    qint64 startPosition() const;
    qint64 endPosition() const;

signals:
    void playPositionChanged(qint64);
    void startPositionChanged(qint64);
    void endPositionChanged(qint64);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    enum Handle { None, Start, Play, End };
    Handle m_activeHandle = None;

    qint64 m_duration = 0;
    qint64 m_start = 0;
    qint64 m_play = 0;
    qint64 m_end = 0;

    int positionToX(qint64 pos) const;
    qint64 xToPosition(int x) const;
};

#endif
