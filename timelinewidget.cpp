#include "timelinewidget.h"
#include <QPainter>
#include <QMouseEvent>

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(40);
    setMouseTracking(true);
}

void TimelineWidget::setDuration(qint64 durationMs)
{
    m_duration = durationMs;
    m_start = 0;
    m_play = 0;
    m_end = durationMs;
    update();
}

void TimelineWidget::setPlayPosition(qint64 positionMs)
{
    // 🔥 Don't fight user while dragging play handle
    if (m_activeHandle == Play)
        return;

    m_play = qBound(m_start, positionMs, m_end);
    update();
}

qint64 TimelineWidget::startPosition() const { return m_start; }
qint64 TimelineWidget::endPosition() const { return m_end; }

int TimelineWidget::positionToX(qint64 pos) const
{
    if (m_duration == 0)
        return 0;

    return int((double(pos) / m_duration) * width());
}

qint64 TimelineWidget::xToPosition(int x) const
{
    if (width() == 0)
        return 0;

    return qBound<qint64>(
        0,
        qint64(double(x) / width() * m_duration),
        m_duration
        );
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int h = height() / 2;

    // Base bar
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 60, 60));
    p.drawRect(0, h - 3, width(), 6);

    // Active (trimmed) range
    int x1 = positionToX(m_start);
    int x2 = positionToX(m_end);
    p.setBrush(QColor(100, 180, 255));
    p.drawRect(x1, h - 3, x2 - x1, 6);

    // Handles
    auto drawHandle = [&](int x, const QColor &color) {
        p.setBrush(color);
        p.drawEllipse(QPoint(x, h), 6, 6);
    };

    drawHandle(x1, Qt::white);                 // start
    drawHandle(positionToX(m_play), Qt::red);  // play
    drawHandle(x2, Qt::white);                 // end
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    const int x = int(e->position().x());

    if (qAbs(x - positionToX(m_start)) < 8) {
        m_activeHandle = Start;
    }
    else if (qAbs(x - positionToX(m_play)) < 8) {
        m_activeHandle = Play;
    }
    else if (qAbs(x - positionToX(m_end)) < 8) {
        m_activeHandle = End;
    }
    else {
        m_activeHandle = None;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_activeHandle == None)
        return;

    const qint64 pos = xToPosition(int(e->position().x()));

    if (m_activeHandle == Start) {
        m_start = qMin(pos, m_end);
        emit startPositionChanged(m_start);
    }
    else if (m_activeHandle == Play) {
        m_play = qBound(m_start, pos, m_end);
        emit playPositionChanged(m_play);  // 🔥 continuous seek
    }
    else if (m_activeHandle == End) {
        m_end = qMax(pos, m_start);
        emit endPositionChanged(m_end);
    }

    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_activeHandle = None;
}
