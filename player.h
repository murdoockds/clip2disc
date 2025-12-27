#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QPushButton;

class Player : public QWidget
{
    Q_OBJECT

public:
    explicit Player(QWidget *parent = nullptr);

    void setSource(const QUrl &url);
    void setSourceFile(const QString &filePath);

private:
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QVideoWidget *m_videoWidget = nullptr;
    QPushButton *m_playPauseButton = nullptr;
};

#endif // PLAYER_H
