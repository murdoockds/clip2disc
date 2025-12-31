#include "timelinewidget.h"
#include <QPainter>
#include <QMouseEvent>

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(70);   // ⬅ wider / thicker timeline
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

    const int trackHeight = 16;
    const int handleRadius = 7;
    const int centerY = height() / 2;

    const int trackTop = centerY - trackHeight / 2;

    // --- Base track ---
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(50, 50, 50));
    p.drawRoundedRect(
        0,
        trackTop,
        width(),
        trackHeight,
        6,
        6
        );

    // --- Active (trim) range ---
    const int xStart = positionToX(m_start);
    const int xEnd   = positionToX(m_end);

    p.setBrush(QColor(100, 180, 255));
    p.drawRoundedRect(
        xStart,
        trackTop,
        xEnd - xStart,
        trackHeight,
        6,
        6
        );

    // --- Start / End handles ---
    auto drawHandle = [&](int x) {
        p.setBrush(Qt::white);
        p.drawEllipse(QPoint(x, centerY), handleRadius, handleRadius);
    };

    drawHandle(xStart);
    drawHandle(xEnd);

    // --- Playhead (vertical line) ---
    const int playX = positionToX(m_play);

    p.setPen(QPen(Qt::red, 2));
    p.drawLine(
        playX,
        trackTop - 8,
        playX,
        trackTop + trackHeight + 8
        );
}


void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    const int x = int(e->position().x());
    const qint64 clickedPos = xToPosition(x);

    const int startX = positionToX(m_start);
    const int playX  = positionToX(m_play);
    const int endX   = positionToX(m_end);

    // 1️⃣ Handle dragging (priority)
    if (qAbs(x - startX) < 8) {
        m_activeHandle = Start;
        return;
    }
    if (qAbs(x - playX) < 8) {
        m_activeHandle = Play;
        return;
    }
    if (qAbs(x - endX) < 8) {
        m_activeHandle = End;
        return;
    }

    // 2️⃣ Click inside active (blue) range → move playhead
    if (clickedPos >= m_start && clickedPos <= m_end) {
        m_play = clickedPos;
        emit playPositionChanged(m_play);
        update();
        return;
    }

    // 3️⃣ Otherwise: nothing
    m_activeHandle = None;
}


void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_activeHandle == None)
        return;

    const qint64 pos = xToPosition(int(e->position().x()));

    if (m_activeHandle == Start) {
        const qint64 newStart = qMin(pos, m_end);

        // 🎯 If start is pushed past playhead, move playhead with it
        if (newStart > m_start && newStart >= m_play) {
            m_play = newStart;
            emit playPositionChanged(m_play);
        }

        m_start = newStart;
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
