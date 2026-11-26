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

    // External control from buttons / presets
    void setStartPosition(qint64 positionMs);
    void setEndPosition(qint64 positionMs);
    void setTrimRange(qint64 startMs, qint64 endMs);

    // Getters
    qint64 duration() const;
    qint64 startPosition() const;
    qint64 endPosition() const;
    qint64 playPosition() const;

signals:
    // Playback scrub / click
    void playPositionChanged(qint64 positionMs);

    // Trim handles / range slide
    void startPositionChanged(qint64 positionMs);
    void endPositionChanged(qint64 positionMs);
    void rangeChanged(qint64 startMs, qint64 endMs);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    enum Handle {
        None,
        Start,
        Play,
        End,
        RangePending,
        RangeSlide
    };

    Handle m_activeHandle = None;
    Handle m_hoverHandle  = None;

    qint64 m_duration = 0;
    qint64 m_start    = 0;
    qint64 m_play     = 0;
    qint64 m_end      = 0;

    // Range drag state
    int    m_dragStartX     = 0;
    qint64 m_dragInitStart  = 0;
    qint64 m_dragInitEnd    = 0;

    // Helpers
    static constexpr int H_MARGIN = 12;
    int trackLeft() const;
    int trackWidth() const;
    int positionToX(qint64 pos) const;
    qint64 xToPosition(int x) const;
    Handle hitTest(int x) const;
    static QString formatMs(qint64 ms);
};

#endif // TIMELINEWIDGET_H
