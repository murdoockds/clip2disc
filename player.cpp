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
#include <QUrl>

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

    // --- Play / Pause button ---
    m_playPauseButton = new QPushButton(tr("Play"), this);

    connect(m_playPauseButton, &QPushButton::clicked, this, [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
            m_playPauseButton->setText(tr("Play"));
        } else {
            m_player->play();
            m_playPauseButton->setText(tr("Pause"));
        }
    });

    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_playPauseButton);
    controlsLayout->addStretch();

    // --- Timeline (single widget, 3 handles) ---
    m_timeline = new TimelineWidget(this);

    connect(m_player, &QMediaPlayer::durationChanged,
            m_timeline, &TimelineWidget::setDuration);

    connect(m_player, &QMediaPlayer::positionChanged,
            m_timeline, &TimelineWidget::setPlayPosition);

    // 🔥 QML-style behavior: continuous seek, no pause/resume
    connect(m_timeline, &TimelineWidget::playPositionChanged,
            this, [this](qint64 pos) {
                m_player->setPosition(pos);
            });

    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::LoadedMedia ||
                    status == QMediaPlayer::BufferedMedia) {

                    m_player->play();
                    m_playPauseButton->setText(tr("Pause"));
                }
            });


    // --- Main layout ---
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(videoContainer, 1); // video + overlay
    mainLayout->addWidget(m_timeline);        // timeline
    mainLayout->addLayout(controlsLayout);    // play/pause

    setLayout(mainLayout);

    // --- Overlay click ---
    connect(m_overlay, &ClickOverlay::clicked,
            this, &Player::requestOpenFile);
}

void Player::setSource(const QUrl &url)
{
    m_player->setSource(url);
    m_overlay->hide();
}

void Player::setSourceFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

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

void Player::pause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        m_playPauseButton->setText(tr("Play"));
    }
}
