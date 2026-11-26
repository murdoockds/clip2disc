#include "timelinewidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(44);
    setMouseTracking(true);
}

int TimelineWidget::trackLeft() const
{
    return H_MARGIN;
}

int TimelineWidget::trackWidth() const
{
    return qMax(1, width() - 2 * H_MARGIN);
}

void TimelineWidget::setDuration(qint64 durationMs)
{
    m_duration = qMax<qint64>(0, durationMs);
    m_start = 0;
    m_play = 0;
    m_end = m_duration;
    update();
}

void TimelineWidget::setPlayPosition(qint64 positionMs)
{
    if (m_activeHandle == Play)
        return;

    m_play = qBound(m_start, positionMs, m_end);
    update();
}

void TimelineWidget::setStartPosition(qint64 positionMs)
{
    m_start = qBound<qint64>(0, positionMs, m_end);

    if (m_play < m_start) {
        m_play = m_start;
        emit playPositionChanged(m_play);
    }

    emit startPositionChanged(m_start);
    emit rangeChanged(m_start, m_end);
    update();
}

void TimelineWidget::setEndPosition(qint64 positionMs)
{
    m_end = qBound<qint64>(m_start, positionMs, m_duration);

    if (m_play > m_end) {
        m_play = m_end;
        emit playPositionChanged(m_play);
    }

    emit endPositionChanged(m_end);
    emit rangeChanged(m_start, m_end);
    update();
}

void TimelineWidget::setTrimRange(qint64 startMs, qint64 endMs)
{
    if (m_duration <= 0)
        return;

    m_start = qBound<qint64>(0, startMs, m_duration);
    m_end   = qBound<qint64>(m_start, endMs, m_duration);

    if (m_play < m_start || m_play > m_end) {
        m_play = m_start;
        emit playPositionChanged(m_play);
    }

    emit startPositionChanged(m_start);
    emit endPositionChanged(m_end);
    emit rangeChanged(m_start, m_end);
    update();
}

qint64 TimelineWidget::duration() const { return m_duration; }
qint64 TimelineWidget::startPosition() const { return m_start; }
qint64 TimelineWidget::endPosition() const { return m_end; }
qint64 TimelineWidget::playPosition() const { return m_play; }

int TimelineWidget::positionToX(qint64 pos) const
{
    if (m_duration <= 0)
        return trackLeft();

    return trackLeft() + int((double(pos) / double(m_duration)) * trackWidth());
}

qint64 TimelineWidget::xToPosition(int x) const
{
    if (m_duration <= 0)
        return 0;

    const double ratio = double(x - trackLeft()) / double(trackWidth());
    return qBound<qint64>(0, qint64(ratio * m_duration), m_duration);
}

TimelineWidget::Handle TimelineWidget::hitTest(int x) const
{
    if (m_duration <= 0)
        return None;

    const int startX = positionToX(m_start);
    const int playX  = positionToX(m_play);
    const int endX   = positionToX(m_end);

    if (qAbs(x - startX) <= 10)
        return Start;
    if (qAbs(x - endX) <= 10)
        return End;
    if (qAbs(x - playX) <= 8)
        return Play;
    if (x > startX + 10 && x < endX - 10)
        return RangeSlide;

    return None;
}

QString TimelineWidget::formatMs(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 totalSec = ms / 1000;
    const qint64 mins = totalSec / 60;
    const qint64 secs = totalSec % 60;
    const qint64 tenths = (ms % 1000) / 100;
    return QString("%1:%2.%3")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(tenths);
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int left = trackLeft();
    const int w = trackWidth();
    const int trackHeight = 20;
    const int centerY = height() / 2 + 2;
    const int trackTop = centerY - trackHeight / 2;

    // --- Base track ---
    const QColor trackColor(0x1E, 0x1F, 0x22);
    const QColor borderColor(0x3F, 0x41, 0x47);
    const QColor tickColor(0x4E, 0x50, 0x58);
    const QColor activeFill(0x58, 0x65, 0xF2, 210);     // Discord Blurple
    const QColor activeHover(0x6D, 0x79, 0xFF, 235);
    const QColor handleColor(0xF2, 0xF3, 0xF5);
    const QColor handleHoverColor(0xFF, 0xFF, 0xFF);
    const QColor playheadColor(0xFF, 0x5A, 0x5F);

    p.setPen(QPen(borderColor, 1));
    p.setBrush(trackColor);
    p.drawRoundedRect(left, trackTop, w, trackHeight, 6, 6);

    if (m_duration <= 0)
        return;

    // --- Subtle time ruler ticks ---
    const int tickCount = qBound(4, w / 60, 20);
    p.setPen(QPen(tickColor, 1));
    for (int i = 1; i < tickCount; ++i) {
        const int tx = left + (w * i) / tickCount;
        p.drawLine(tx, trackTop + 4, tx, trackTop + trackHeight - 4);
    }

    // --- Active (trim) range ---
    const int xStart = positionToX(m_start);
    const int xEnd   = positionToX(m_end);
    const int rangeW = qMax(2, xEnd - xStart);

    const bool sliding = (m_activeHandle == RangeSlide || m_hoverHandle == RangeSlide);
    p.setPen(QPen(QColor(0x8E, 0x99, 0xFF), 1.5));
    p.setBrush(sliding ? activeHover : activeFill);
    p.drawRoundedRect(xStart, trackTop, rangeW, trackHeight, 5, 5);

    // Draw subtle drag-grip lines in the center of the selected window if wide enough
    if (rangeW >= 36) {
        const int midX = (xStart + xEnd) / 2;
        p.setPen(QPen(QColor(255, 255, 255, 130), 1.5));
        for (int dx : {-4, 0, 4}) {
            p.drawLine(midX + dx, centerY - 4, midX + dx, centerY + 4);
        }
    }

    // --- Start / End trim handles (vertical pills) ---
    auto drawTrimHandle = [&](int hx, bool highlighted) {
        const int hw = highlighted ? 10 : 8;
        const int hh = trackHeight + 6;
        p.setPen(QPen(QColor(0x2B, 0x2D, 0x31), 1.5));
        p.setBrush(highlighted ? handleHoverColor : handleColor);
        p.drawRoundedRect(hx - hw / 2, centerY - hh / 2, hw, hh, 4, 4);
    };

    drawTrimHandle(xStart, m_activeHandle == Start || m_hoverHandle == Start);
    drawTrimHandle(xEnd, m_activeHandle == End || m_hoverHandle == End);

    // --- Playhead line + top pointer triangle ---
    const int playX = positionToX(m_play);
    p.setPen(QPen(playheadColor, 2));
    p.drawLine(playX, trackTop - 6, playX, trackTop + trackHeight + 5);

    QPainterPath head;
    head.moveTo(playX, trackTop - 2);
    head.lineTo(playX - 5, trackTop - 9);
    head.lineTo(playX + 5, trackTop - 9);
    head.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(playheadColor);
    p.drawPath(head);
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    if (m_duration <= 0)
        return;

    const int x = int(e->position().x());
    const Handle h = hitTest(x);

    if (h == Start || h == End || h == Play) {
        m_activeHandle = h;
        update();
        return;
    }

    if (h == RangeSlide) {
        // Click seeks on release; dragging horizontally > 4px slides the whole clip window!
        m_activeHandle = RangePending;
        m_dragStartX = x;
        m_dragInitStart = m_start;
        m_dragInitEnd = m_end;
        return;
    }

    // Clicked outside active range: seek & expand/move? Just ignore outside handles
    m_activeHandle = None;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_duration <= 0)
        return;

    const int x = int(e->position().x());

    if (m_activeHandle == None) {
        const Handle h = hitTest(x);
        if (h != m_hoverHandle) {
            m_hoverHandle = h;
            if (h == Start || h == End)
                setCursor(Qt::SizeHorCursor);
            else if (h == RangeSlide)
                setCursor(Qt::OpenHandCursor);
            else if (h == Play)
                setCursor(Qt::PointingHandCursor);
            else
                setCursor(Qt::ArrowCursor);
            update();
        }
        return;
    }

    if (m_activeHandle == RangePending) {
        if (qAbs(x - m_dragStartX) >= 4) {
            m_activeHandle = RangeSlide;
            setCursor(Qt::ClosedHandCursor);
        } else {
            return;
        }
    }

    if (m_activeHandle == RangeSlide) {
        const qint64 windowLen = m_dragInitEnd - m_dragInitStart;
        const qint64 deltaMs = qint64((double(x - m_dragStartX) / double(trackWidth())) * m_duration);
        qint64 newStart = qBound<qint64>(0, m_dragInitStart + deltaMs, m_duration - windowLen);
        qint64 newEnd = newStart + windowLen;

        m_start = newStart;
        m_end = newEnd;
        m_play = newStart;

        emit startPositionChanged(m_start);
        emit endPositionChanged(m_end);
        emit rangeChanged(m_start, m_end);
        emit playPositionChanged(m_play);

        QToolTip::showText(
            e->globalPosition().toPoint(),
            QString("%1 - %2 (%3s)")
                .arg(formatMs(m_start))
                .arg(formatMs(m_end))
                .arg(windowLen / 1000.0, 0, 'f', 1),
            this);
        update();
        return;
    }

    const qint64 pos = xToPosition(x);

    if (m_activeHandle == Start) {
        const qint64 newStart = qMin(pos, m_end);

        if (newStart > m_start && newStart >= m_play) {
            m_play = newStart;
            emit playPositionChanged(m_play);
        }

        m_start = newStart;
        emit startPositionChanged(m_start);
        emit rangeChanged(m_start, m_end);

        QToolTip::showText(e->globalPosition().toPoint(),
                           "In: " + formatMs(m_start), this);
    }
    else if (m_activeHandle == Play) {
        m_play = qBound(m_start, pos, m_end);
        emit playPositionChanged(m_play);

        QToolTip::showText(e->globalPosition().toPoint(),
                           formatMs(m_play), this);
    }
    else if (m_activeHandle == End) {
        const qint64 newEnd = qMax(pos, m_start);

        if (newEnd < m_play) {
            m_play = newEnd;
            emit playPositionChanged(m_play);
        }

        m_end = newEnd;
        emit endPositionChanged(m_end);
        emit rangeChanged(m_start, m_end);

        QToolTip::showText(e->globalPosition().toPoint(),
                           "Out: " + formatMs(m_end), this);
    }

    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_activeHandle == RangePending) {
        // Simple click inside active selection -> seek playhead there
        const qint64 clickedPos = xToPosition(int(e->position().x()));
        m_play = qBound(m_start, clickedPos, m_end);
        emit playPositionChanged(m_play);
    }

    m_activeHandle = None;
    m_hoverHandle = hitTest(int(e->position().x()));
    if (m_hoverHandle == Start || m_hoverHandle == End)
        setCursor(Qt::SizeHorCursor);
    else if (m_hoverHandle == RangeSlide)
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::ArrowCursor);
    update();
}

void TimelineWidget::leaveEvent(QEvent *)
{
    if (m_activeHandle == None) {
        m_hoverHandle = None;
        setCursor(Qt::ArrowCursor);
        update();
    }
}
