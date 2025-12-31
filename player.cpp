#include "player.h"
#include "clickoverlay.h"
#include "timelinewidget.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QStyle>

static constexpr qint64 FRAME_STEP_MS = 40; // ~25fps fallback

Player::Player(QWidget *parent)
    : QWidget(parent)
{
    // --- Media objects ---
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    // --- Video widget ---
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setStyleSheet("background-color: black;");
    m_player->setVideoOutput(m_videoWidget);

    // --- Overlay ---
    m_overlay = new ClickOverlay(this);

    // --- Stack video + overlay ---
    auto *videoStack = new QStackedLayout;
    videoStack->setContentsMargins(0, 0, 0, 0);
    videoStack->setStackingMode(QStackedLayout::StackAll);
    videoStack->addWidget(m_videoWidget);
    videoStack->addWidget(m_overlay);

    auto *videoContainer = new QWidget(this);
    videoContainer->setLayout(videoStack);

    // --- Timeline ---
    m_timeline = new TimelineWidget(this);
    m_timeline->setEnabled(false);

    connect(m_player, &QMediaPlayer::durationChanged,
            m_timeline, &TimelineWidget::setDuration);

    // --- Autoplay ONCE after media loads ---
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {

                if ((status == QMediaPlayer::LoadedMedia ||
                     status == QMediaPlayer::BufferedMedia) &&
                    m_autoPlayPending) {

                    m_autoPlayPending = false;
                    m_hasMedia = true;

                    updateControlsEnabled(true);

                    m_player->setPosition(m_timeline->startPosition());
                    play();
                }
            });

    // --- Stop cleanly at trim end ---
    connect(m_player, &QMediaPlayer::positionChanged,
            this, [this](qint64 pos) {

                if (m_reachedTrimEnd)
                    return;

                const qint64 end = m_timeline->endPosition();

                if (m_player->playbackState() == QMediaPlayer::PlayingState &&
                    pos >= end) {

                    m_player->pause();
                    m_player->setPosition(end);
                    m_reachedTrimEnd = true;

                    m_btnPlayPause->setIcon(
                        style()->standardIcon(QStyle::SP_MediaPlay)
                        );
                    return;
                }

                m_timeline->setPlayPosition(pos);
            });

    // --- Continuous seek from timeline ---
    connect(m_timeline, &TimelineWidget::playPositionChanged,
            this, [this](qint64 pos) {
                m_player->setPosition(pos);
            });

    // --- Buttons ---
    m_btnGoToStart = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaSkipBackward), "", this);
    m_btnPrevFrame = new QPushButton("⏮︎", this);
    m_btnSetStart  = new QPushButton("[", this);
    m_btnPlayPause = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaPlay), "", this);
    m_btnSetEnd    = new QPushButton("]", this);
    m_btnNextFrame = new QPushButton("⏭︎", this);
    m_btnStop      = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaStop), "", this);

    // --- Button connections ---
    connect(m_btnGoToStart, &QPushButton::clicked,
            this, &Player::goToStart);
    connect(m_btnPrevFrame, &QPushButton::clicked,
            this, &Player::stepFrameBackward);
    connect(m_btnNextFrame, &QPushButton::clicked,
            this, &Player::stepFrameForward);
    connect(m_btnSetStart, &QPushButton::clicked,
            this, &Player::markStartAtCurrentFrame);
    connect(m_btnSetEnd, &QPushButton::clicked,
            this, &Player::markEndAtCurrentFrame);
    connect(m_btnStop, &QPushButton::clicked,
            this, &Player::stop);

    connect(m_btnPlayPause, &QPushButton::clicked, this, [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            pause();
        } else {
            if (m_reachedTrimEnd) {
                m_player->setPosition(m_timeline->startPosition());
                m_reachedTrimEnd = false;
            }
            play();
        }
    });

    // --- Controls layout ---
    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_btnGoToStart);
    controlsLayout->addWidget(m_btnSetStart);
    controlsLayout->addWidget(m_btnPrevFrame);
    controlsLayout->addWidget(m_btnPlayPause);
    controlsLayout->addWidget(m_btnNextFrame);
    controlsLayout->addWidget(m_btnSetEnd);
    controlsLayout->addWidget(m_btnStop);
    controlsLayout->addStretch();

    // --- Main layout ---
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(videoContainer, 1);
    mainLayout->addWidget(m_timeline);
    mainLayout->addLayout(controlsLayout);
    setLayout(mainLayout);

    // --- Overlay click ---
    connect(m_overlay, &ClickOverlay::clicked,
            this, &Player::requestOpenFile);

    updateControlsEnabled(false);
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
    m_timeline->setEnabled(enabled);
}

// ----------------- Playback helpers -----------------

void Player::play()
{
    m_overlay->hide();          // reveal video
    m_player->play();

    m_btnPlayPause->setIcon(
        style()->standardIcon(QStyle::SP_MediaPause)
        );
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

    // Show black overlay, but WITHOUT text
    m_overlay->show();
    m_overlay->showText(false);

    m_btnPlayPause->setIcon(
        style()->standardIcon(QStyle::SP_MediaPlay)
        );

    m_reachedTrimEnd = false;
}


void Player::goToStart()
{
    pause();
    m_reachedTrimEnd = false;
    m_player->setPosition(m_timeline->startPosition());
}

void Player::stepFrameBackward()
{
    pause();
    qint64 pos = m_player->position() - FRAME_STEP_MS;
    pos = qMax(pos, m_timeline->startPosition());
    m_player->setPosition(pos);
}

void Player::stepFrameForward()
{
    pause();
    qint64 pos = m_player->position() + FRAME_STEP_MS;
    pos = qMin(pos, m_timeline->endPosition());
    m_player->setPosition(pos);
}

void Player::markStartAtCurrentFrame()
{
    m_timeline->setStartPosition(m_player->position());
}

void Player::markEndAtCurrentFrame()
{
    m_timeline->setEndPosition(m_player->position());
}

// ----------------- Source -----------------

void Player::setSource(const QUrl &url)
{
    m_autoPlayPending = true;
    m_reachedTrimEnd = false;
    m_player->setSource(url);
    m_overlay->hide();
}

void Player::setSourceFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    m_autoPlayPending = true;
    m_reachedTrimEnd = false;
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
