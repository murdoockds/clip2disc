#include "player.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

Player::Player(QWidget *parent)
    : QWidget(parent)
{
    // --- Core media objects ---
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    m_videoWidget = new QVideoWidget(this);
    m_player->setVideoOutput(m_videoWidget);

    // --- Controls ---
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

    // --- Layout ---
    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->addStretch();
    controlsLayout->addWidget(m_playPauseButton);
    controlsLayout->addStretch();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_videoWidget, 1);
    mainLayout->addLayout(controlsLayout);

    setLayout(mainLayout);
}

void Player::setSource(const QUrl &url)
{
    m_player->setSource(url);
}

#include <QUrl>

void Player::setSourceFile(const QString &filePath)
{
    m_player->setSource(QUrl::fromLocalFile(filePath));
    m_player->play();
}
