#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>
#include <QUrl>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QPushButton;
class ClickOverlay;
class TimelineWidget;

class Player : public QWidget
{
    Q_OBJECT

public:
    void pause();
    explicit Player(QWidget *parent = nullptr);

    void setSource(const QUrl &url);
    void setSourceFile(const QString &filePath);

    // Expose trim values for MainWindow later
    qint64 trimStart() const;
    qint64 trimEnd() const;

signals:
    // Emitted when user clicks the overlay
    void requestOpenFile();

private:
    bool m_autoPlayPending;
    bool m_reachedTrimEnd = false;

    // Core media objects
    QMediaPlayer  *m_player = nullptr;
    QAudioOutput  *m_audioOutput = nullptr;
    QVideoWidget  *m_videoWidget = nullptr;

    // UI
    QPushButton   *m_playPauseButton = nullptr;
    ClickOverlay  *m_overlay = nullptr;
    TimelineWidget *m_timeline = nullptr;
};

#endif // PLAYER_H
