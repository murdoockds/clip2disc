#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>


class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QPushButton;
class ClickOverlay;


class Player : public QWidget
{
    Q_OBJECT

public:
    explicit Player(QWidget *parent = nullptr);

    void setSource(const QUrl &url);
    void setSourceFile(const QString &filePath);

signals:
    // Emitted when user clicks the overlay
    void requestOpenFile();

private:
    // Core media objects
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QVideoWidget *m_videoWidget = nullptr;

    // UI
    QPushButton *m_playPauseButton = nullptr;
    ClickOverlay *m_overlay = nullptr;

};

#endif // PLAYER_H
