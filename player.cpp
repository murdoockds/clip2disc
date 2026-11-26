#include "player.h"
#include "clickoverlay.h"
#include "timelinewidget.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QStyle>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QtMath>

static constexpr qint64 DEFAULT_FRAME_STEP_MS = 40; // ~25fps fallback
static constexpr qint64 CLIP_WINDOW_30S_MS    = 30000;

QString Player::formatTimestamp(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 totalSec = ms / 1000;
    const qint64 mins = totalSec / 60;
    const qint64 secs = totalSec % 60;
    const qint64 tenths = (ms % 1000) / 100;
    return QString("%1:%2.%3")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(tenths);
}

Player::Player(QWidget *parent)
    : QWidget(parent)
    , m_frameStepMs(DEFAULT_FRAME_STEP_MS)
{
    // --- Media objects ---
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    // --- Video widget ---
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setStyleSheet("background-color: #111214; border-radius: 6px;");
    m_player->setVideoOutput(m_videoWidget);

    if (auto *sink = m_videoWidget->videoSink()) {
        connect(sink, &QVideoSink::videoFrameChanged,
                this, [this](const QVideoFrame &frame) {
                    if (frame.isValid()) {
                        m_receivedFrame = true;
                    }
                });
    }

    // --- Video container ---
    auto *videoContainer = new QWidget(this);
    auto *videoStack = new QStackedLayout(videoContainer);
    videoStack->setContentsMargins(0, 0, 0, 0);
    videoStack->setStackingMode(QStackedLayout::StackAll);
    videoStack->addWidget(m_videoWidget);
    videoContainer->setLayout(videoStack);

    // --- Overlay ---
    m_overlay = new ClickOverlay(m_videoWidget);
    m_overlay->show();
    m_overlay->raise();

    // --- Time info strip ---
    m_inTimeLabel = new QLabel("In: 00:00.0", this);
    m_inTimeLabel->setObjectName("timeBadge");

    m_currentTimeLabel = new QLabel("00:00.0 / 00:00.0  •  Clip: 0.0s", this);
    m_currentTimeLabel->setObjectName("timeCurrent");
    m_currentTimeLabel->setAlignment(Qt::AlignCenter);

    m_outTimeLabel = new QLabel("Out: 00:00.0", this);
    m_outTimeLabel->setObjectName("timeBadge");
    m_outTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *timeLayout = new QHBoxLayout;
    timeLayout->setContentsMargins(6, 2, 6, 0);
    timeLayout->addWidget(m_inTimeLabel);
    timeLayout->addStretch();
    timeLayout->addWidget(m_currentTimeLabel);
    timeLayout->addStretch();
    timeLayout->addWidget(m_outTimeLabel);

    // --- Timeline ---
    m_timeline = new TimelineWidget(this);
    m_timeline->setEnabled(false);
    m_timeline->setToolTip("Drag start/end handles to trim, or drag the highlighted bar to slide your clip window");

    connect(m_player, &QMediaPlayer::durationChanged,
            this, [this](qint64 durationMs) {
                if (durationMs <= 0)
                    return;
                if (m_usingProxy && m_timeline->duration() > 0)
                    return;
                if (m_timeline->duration() <= 0) {
                    m_timeline->setDuration(durationMs);
                    updateTimeLabels();
                    emit trimChanged(m_timeline->startPosition(), m_timeline->endPosition());
                }
            });

    connect(m_player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error error, const QString &errorString) {
                if (error == QMediaPlayer::NoError)
                    return;

                qWarning() << "QMediaPlayer error:" << error << errorString;
                m_autoPlayPending = false;

                if (!m_usingProxy && !m_failureReported) {
                    m_failureReported = true;
                    emit playbackFailed(errorString.isEmpty()
                                            ? QString("Media playback error (%1)").arg(int(error))
                                            : errorString);
                } else {
                    setOverlayStatus("Preview unavailable (you can still trim & compress)", false);
                }
            });

    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::InvalidMedia) {
                    m_autoPlayPending = false;
                    if (!m_usingProxy && !m_failureReported) {
                        m_failureReported = true;
                        emit playbackFailed("Unsupported media format or codec");
                    } else {
                        setOverlayStatus("Preview unavailable (you can still trim & compress)", false);
                    }
                    return;
                }

                if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) &&
                    m_autoPlayPending) {
                    m_autoPlayPending = false;
                    m_hasMedia = true;
                    updateControlsEnabled(true);

                    m_player->setPosition(m_timeline->startPosition());
                    play();

                    if (m_expectVideo && !m_usingProxy) {
                        const int gen = m_loadGeneration;
                        QTimer::singleShot(1200, this, [this, gen]() {
                            checkVideoTrackLoaded(gen);
                        });
                    }
                }
            });

    connect(m_player, &QMediaPlayer::positionChanged,
            this, [this](qint64 pos) {
                if (m_reachedTrimEnd)
                    return;

                const qint64 end = m_timeline->endPosition();
                if (m_player->playbackState() == QMediaPlayer::PlayingState && end > 0 && pos >= end) {
                    m_player->pause();
                    m_player->setPosition(end);
                    m_reachedTrimEnd = true;
                    m_btnPlayPause->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
                    m_timeline->setPlayPosition(end);
                    updateTimeLabels();
                    return;
                }
                m_timeline->setPlayPosition(pos);
                updateTimeLabels();
            });

    connect(m_timeline, &TimelineWidget::playPositionChanged,
            this, [this](qint64 pos) {
                m_reachedTrimEnd = false;
                m_player->setPosition(pos);
                updateTimeLabels();
            });

    // --- Playback buttons ---
    m_btnGoToStart = new QPushButton(this);
    m_btnGoToStart->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    m_btnGoToStart->setToolTip("Jump to clip start (Home)");
    m_btnGoToStart->setFixedSize(32, 30);

    m_btnSetStart = new QPushButton("[ In", this);
    m_btnSetStart->setToolTip("Set trim start at current frame (I)");
    m_btnSetStart->setFixedHeight(30);

    m_btnPrevFrame = new QPushButton(this);
    m_btnPrevFrame->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    m_btnPrevFrame->setToolTip("Previous frame (Left Arrow)");
    m_btnPrevFrame->setFixedSize(32, 30);

    m_btnPlayPause = new QPushButton(this);
    m_btnPlayPause->setObjectName("playPauseButton");
    m_btnPlayPause->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_btnPlayPause->setToolTip("Play / Pause (Space)");
    m_btnPlayPause->setFixedSize(42, 30);

    m_btnNextFrame = new QPushButton(this);
    m_btnNextFrame->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    m_btnNextFrame->setToolTip("Next frame (Right Arrow)");
    m_btnNextFrame->setFixedSize(32, 30);

    m_btnSetEnd = new QPushButton("Out ]", this);
    m_btnSetEnd->setToolTip("Set trim end at current frame (O)");
    m_btnSetEnd->setFixedHeight(30);

    m_btnStop = new QPushButton(this);
    m_btnStop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_btnStop->setToolTip("Stop");
    m_btnStop->setFixedSize(32, 30);

    // --- Quick clip-window presets ---
    m_btnLast30s = new QPushButton("Last 30s", this);
    m_btnLast30s->setObjectName("presetButton");
    m_btnLast30s->setToolTip("Select the last 30 seconds (then drag the bar on the timeline to adjust)");
    m_btnLast30s->setFixedHeight(30);

    m_btn30sHere = new QPushButton("30s Here", this);
    m_btn30sHere->setObjectName("presetButton");
    m_btn30sHere->setToolTip("Select a 30-second window starting at the current playhead");
    m_btn30sHere->setFixedHeight(30);

    m_btnFullClip = new QPushButton("Full", this);
    m_btnFullClip->setObjectName("presetButton");
    m_btnFullClip->setToolTip("Reset selection to the entire video");
    m_btnFullClip->setFixedHeight(30);

    connect(m_btnGoToStart, &QPushButton::clicked, this, &Player::goToStart);
    connect(m_btnPrevFrame, &QPushButton::clicked, this, &Player::stepFrameBackward);
    connect(m_btnNextFrame, &QPushButton::clicked, this, &Player::stepFrameForward);
    connect(m_btnSetStart, &QPushButton::clicked, this, &Player::markStartAtCurrentFrame);
    connect(m_btnSetEnd, &QPushButton::clicked, this, &Player::markEndAtCurrentFrame);
    connect(m_btnStop, &QPushButton::clicked, this, &Player::stop);
    connect(m_btnPlayPause, &QPushButton::clicked, this, &Player::togglePlayPause);

    connect(m_btnLast30s, &QPushButton::clicked, this, &Player::selectLast30Seconds);
    connect(m_btn30sHere, &QPushButton::clicked, this, &Player::select30SecondsFromHere);
    connect(m_btnFullClip, &QPushButton::clicked, this, &Player::selectFullVideo);

    // --- Volume slider ---
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setFixedWidth(85);
    m_volumeSlider->setToolTip("Preview Volume");
    m_audioOutput->setVolume(0.5);

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value){
        m_audioOutput->setVolume(value / 100.0);
    });

    QLabel *volumeLabel = new QLabel("🔊", this);
    volumeLabel->setAlignment(Qt::AlignCenter);
    volumeLabel->setFixedWidth(22);

    // --- Controls layout ---
    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->setContentsMargins(4, 2, 4, 4);
    controlsLayout->setSpacing(6);
    controlsLayout->addWidget(volumeLabel);
    controlsLayout->addWidget(m_volumeSlider);
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_btnGoToStart);
    controlsLayout->addWidget(m_btnSetStart);
    controlsLayout->addWidget(m_btnPrevFrame);
    controlsLayout->addWidget(m_btnPlayPause);
    controlsLayout->addWidget(m_btnNextFrame);
    controlsLayout->addWidget(m_btnSetEnd);
    controlsLayout->addWidget(m_btnStop);
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_btnLast30s);
    controlsLayout->addWidget(m_btn30sHere);
    controlsLayout->addWidget(m_btnFullClip);

    // --- Main layout ---
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);
    mainLayout->addWidget(videoContainer, 1);
    mainLayout->addLayout(timeLayout);
    mainLayout->addWidget(m_timeline);
    mainLayout->addLayout(controlsLayout);
    setLayout(mainLayout);

    // --- Overlay click ---
    connect(m_overlay, &ClickOverlay::clicked, this, &Player::requestOpenFile);

    updateControlsEnabled(false);

    connect(m_timeline, &TimelineWidget::rangeChanged, this, [this](qint64 start, qint64 end){
        updateTimeLabels();
        emit trimChanged(start, end);
    });
}

void Player::updateTimeLabels()
{
    const qint64 dur = m_timeline->duration();
    const qint64 start = m_timeline->startPosition();
    const qint64 end = m_timeline->endPosition();
    const qint64 pos = m_timeline->playPosition();
    const double clipSec = qMax<qint64>(0, end - start) / 1000.0;

    m_inTimeLabel->setText("In: " + formatTimestamp(start));
    m_outTimeLabel->setText("Out: " + formatTimestamp(end));
    m_currentTimeLabel->setText(
        QString("%1 / %2  •  Clip: %3s")
            .arg(formatTimestamp(pos))
            .arg(formatTimestamp(dur))
            .arg(clipSec, 0, 'f', 1));
}

void Player::checkVideoTrackLoaded(int generation)
{
    if (generation != m_loadGeneration || m_usingProxy || m_failureReported)
        return;

    if (!m_player->hasVideo() || !m_receivedFrame) {
        qWarning() << "QMediaPlayer produced no video frames (hasVideo=" << m_player->hasVideo()
                   << "receivedFrame=" << m_receivedFrame << "); requesting proxy fallback";
        m_player->stop();
        m_failureReported = true;
        emit playbackFailed("Video codec not supported by preview player");
        return;
    }
}

void Player::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_overlay) {
        m_overlay->setGeometry(m_overlay->parentWidget()->rect());
        m_overlay->raise();
    }
}

// ----------------- UI State -----------------

void Player::updateControlsEnabled(bool enabled)
{
    m_btnGoToStart->setEnabled(enabled);
    m_btnPrevFrame->setEnabled(enabled);
    m_btnSetStart->setEnabled(enabled);
    m_btnPlayPause->setEnabled(enabled);
    m_btnSetEnd->setEnabled(enabled);
    m_btnNextFrame->setEnabled(enabled);
    m_btnStop->setEnabled(enabled);
    m_btnLast30s->setEnabled(enabled);
    m_btn30sHere->setEnabled(enabled);
    m_btnFullClip->setEnabled(enabled);
    m_timeline->setEnabled(enabled);
}

void Player::setExternalDuration(qint64 durationMs)
{
    if (durationMs <= 0)
        return;

    m_timeline->setDuration(durationMs);
    m_timeline->setEnabled(true);
    m_btnLast30s->setEnabled(true);
    m_btn30sHere->setEnabled(true);
    m_btnFullClip->setEnabled(true);
    updateTimeLabels();
    emit trimChanged(m_timeline->startPosition(), m_timeline->endPosition());
}

void Player::setFrameRate(double fps)
{
    if (fps > 0.5 && fps <= 240.0) {
        m_frameStepMs = qMax<qint64>(1, qRound64(1000.0 / fps));
    } else {
        m_frameStepMs = DEFAULT_FRAME_STEP_MS;
    }
}

void Player::setOverlayStatus(const QString &text, bool clickable)
{
    if (!m_overlay)
        return;
    m_overlay->setText(text);
    m_overlay->setClickable(clickable);
    m_overlay->showText(true);
    m_overlay->show();
    m_overlay->raise();
}

// ----------------- Playback helpers -----------------

void Player::togglePlayPause()
{
    if (!m_hasMedia)
        return;

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        pause();
    } else {
        if (m_reachedTrimEnd) {
            m_player->setPosition(m_timeline->startPosition());
            m_reachedTrimEnd = false;
        }
        play();
    }
}

void Player::play()
{
    m_overlay->hide();
    m_player->play();
    m_btnPlayPause->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
}

void Player::pause()
{
    if (!m_player->videoOutput()) {
        m_player->setVideoOutput(m_videoWidget);
    }

    m_player->pause();
    m_btnPlayPause->setIcon(
        style()->standardIcon(QStyle::SP_MediaPlay)
        );
}

void Player::stop()
{
    m_player->stop();
    m_overlay->setText("Drop a video here or click to select\nMP4 • MKV • WebM • AVI • MOV");
    m_overlay->setClickable(true);
    m_overlay->show();
    m_btnPlayPause->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_reachedTrimEnd = false;
}

void Player::goToStart()
{
    pause();
    m_reachedTrimEnd = false;
    m_player->setPosition(m_timeline->startPosition());
    m_timeline->setPlayPosition(m_timeline->startPosition());
    updateTimeLabels();
}

void Player::stepFrameBackward()
{
    if (!m_hasMedia)
        return;
    pause();
    qint64 pos = m_player->position() - m_frameStepMs;
    pos = qMax(pos, m_timeline->startPosition());
    m_player->setPosition(pos);
    m_timeline->setPlayPosition(pos);
    updateTimeLabels();
}

void Player::stepFrameForward()
{
    if (!m_hasMedia)
        return;
    pause();
    qint64 pos = m_player->position() + m_frameStepMs;
    pos = qMin(pos, m_timeline->endPosition());
    m_player->setPosition(pos);
    m_timeline->setPlayPosition(pos);
    updateTimeLabels();
}

void Player::markStartAtCurrentFrame()
{
    if (!m_hasMedia)
        return;
    m_timeline->setStartPosition(m_player->position());
}

void Player::markEndAtCurrentFrame()
{
    if (!m_hasMedia)
        return;
    m_timeline->setEndPosition(m_player->position());
}

void Player::selectLast30Seconds()
{
    const qint64 dur = m_timeline->duration();
    if (dur <= 0)
        return;
    const qint64 start = qMax<qint64>(0, dur - CLIP_WINDOW_30S_MS);
    m_reachedTrimEnd = false;
    m_timeline->setTrimRange(start, dur);
    if (m_hasMedia) {
        m_player->setPosition(start);
    }
}

void Player::select30SecondsFromHere()
{
    const qint64 dur = m_timeline->duration();
    if (dur <= 0)
        return;
    qint64 start = m_hasMedia ? m_player->position() : m_timeline->playPosition();
    qint64 end = start + CLIP_WINDOW_30S_MS;
    if (end > dur) {
        end = dur;
        start = qMax<qint64>(0, dur - CLIP_WINDOW_30S_MS);
    }
    m_reachedTrimEnd = false;
    m_timeline->setTrimRange(start, end);
    if (m_hasMedia) {
        m_player->setPosition(start);
    }
}

void Player::selectFullVideo()
{
    const qint64 dur = m_timeline->duration();
    if (dur <= 0)
        return;
    m_reachedTrimEnd = false;
    m_timeline->setTrimRange(0, dur);
    if (m_hasMedia) {
        m_player->setPosition(0);
    }
}

// ----------------- Source -----------------

void Player::clearSource()
{
    ++m_loadGeneration;
    m_hasMedia = false;
    m_autoPlayPending = false;
    m_reachedTrimEnd = false;
    m_usingProxy = false;
    m_failureReported = false;
    m_receivedFrame = false;
    m_player->stop();
    m_player->setSource(QUrl());
}

void Player::setSource(const QUrl &url)
{
    ++m_loadGeneration;
    m_autoPlayPending = true;
    m_reachedTrimEnd = false;
    m_expectVideo = true;
    m_usingProxy = false;
    m_failureReported = false;
    m_receivedFrame = false;
    m_player->setSource(url);
    m_overlay->hide();
}

void Player::setSourceFile(const QString &filePath, bool expectVideo)
{
    if (filePath.isEmpty())
        return;

    ++m_loadGeneration;
    m_autoPlayPending = true;
    m_reachedTrimEnd = false;
    m_expectVideo = expectVideo;
    m_usingProxy = false;
    m_failureReported = false;
    m_receivedFrame = false;
    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_overlay->hide();
}

void Player::setPreviewSourceFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    ++m_loadGeneration;
    m_autoPlayPending = true;
    m_reachedTrimEnd = false;
    m_expectVideo = true;
    m_usingProxy = true;
    m_failureReported = false;
    m_receivedFrame = false;
    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_overlay->hide();
}

qint64 Player::trimStart() const
{
    return m_timeline->startPosition();
}

qint64 Player::trimEnd() const
{
    return m_timeline->endPosition();
}
