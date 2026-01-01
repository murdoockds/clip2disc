#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QWidget>

class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    // Core setters
    void setDuration(qint64 durationMs);
    void setPlayPosition(qint64 positionMs);

    // External control from buttons
    void setStartPosition(qint64 positionMs);
    void setEndPosition(qint64 positionMs);

    // Getters
    qint64 startPosition() const;
    qint64 endPosition() const;

signals:
    // Playback scrub / click
    void playPositionChanged(qint64 positionMs);

    // Trim handles
    void startPositionChanged(qint64 positionMs);
    void endPositionChanged(qint64 positionMs);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    enum Handle {
        None,
        Start,
        Play,
        End
    };

    Handle m_activeHandle = None;

    qint64 m_duration = 0;
    qint64 m_start = 0;
    qint64 m_play = 0;
    qint64 m_end = 0;

    // Helpers
    int positionToX(qint64 pos) const;
    qint64 xToPosition(int x) const;
};

#endif // TIMELINEWIDGET_H
