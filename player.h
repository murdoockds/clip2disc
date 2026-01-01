#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>
#include <QUrl>
#include <QSlider>

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
    explicit Player(QWidget *parent = nullptr);

    void setSource(const QUrl &url);
    void setSourceFile(const QString &filePath);

    void pause();

    // Expose trim values for MainWindow later
    qint64 trimStart() const;
    qint64 trimEnd() const;

signals:
    void trimChanged(qint64 startMs, qint64 endMs);

    // Emitted when user clicks the overlay
    void requestOpenFile();

private:
    void updateControlsEnabled(bool enabled);
    bool m_hasMedia = false;

    // --- Playback helpers ---
    void play();
    void stop();
    void goToStart();
    void stepFrameForward();
    void stepFrameBackward();
    void markStartAtCurrentFrame();
    void markEndAtCurrentFrame();

    // --- State ---
    bool m_autoPlayPending = false;
    bool m_reachedTrimEnd  = false;

    // --- Core media objects ---
    QMediaPlayer   *m_player      = nullptr;
    QAudioOutput   *m_audioOutput = nullptr;
    QVideoWidget   *m_videoWidget = nullptr;

    // --- UI ---
    TimelineWidget *m_timeline        = nullptr;
    ClickOverlay   *m_overlay         = nullptr;

    QPushButton    *m_btnGoToStart    = nullptr;
    QPushButton    *m_btnPrevFrame    = nullptr;
    QPushButton    *m_btnSetStart     = nullptr;
    QPushButton    *m_btnPlayPause    = nullptr;
    QPushButton    *m_btnSetEnd       = nullptr;
    QPushButton    *m_btnNextFrame    = nullptr;
    QPushButton    *m_btnStop         = nullptr;

    QSlider *m_volumeSlider;  // <--- new
};

#endif // PLAYER_H
