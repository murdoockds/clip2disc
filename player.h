#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>
#include <QUrl>
#include <QSlider>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QPushButton;
class QLabel;
class ClickOverlay;
class TimelineWidget;

class Player : public QWidget
{
    Q_OBJECT

public:
    explicit Player(QWidget *parent = nullptr);

    void setSource(const QUrl &url);
    void setSourceFile(const QString &filePath, bool expectVideo = true);
    void setPreviewSourceFile(const QString &filePath);
    void clearSource();

    void setExternalDuration(qint64 durationMs);
    void setFrameRate(double fps);
    void setOverlayStatus(const QString &text, bool clickable = false);

    bool hasMedia() const { return m_hasMedia; }

    qint64 trimStart() const;
    qint64 trimEnd() const;

public slots:
    void pause();
    void togglePlayPause();
    void goToStart();
    void stepFrameForward();
    void stepFrameBackward();
    void markStartAtCurrentFrame();
    void markEndAtCurrentFrame();
    void selectLast30Seconds();
    void select30SecondsFromHere();
    void selectFullVideo();

signals:
    void trimChanged(qint64 startMs, qint64 endMs);

    // Emitted when user clicks the overlay
    void requestOpenFile();

    // Emitted when QMediaPlayer fails to decode/display the selected media (e.g. AV1)
    void playbackFailed(const QString &reason);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateControlsEnabled(bool enabled);
    void updateTimeLabels();
    void checkVideoTrackLoaded(int generation);
    static QString formatTimestamp(qint64 ms);

    bool m_hasMedia = false;

    // --- Playback helpers ---
    void play();
    void stop();

    // --- State ---
    bool m_autoPlayPending   = false;
    bool m_reachedTrimEnd    = false;
    bool m_expectVideo       = true;
    bool m_usingProxy        = false;
    bool m_failureReported   = false;
    bool m_receivedFrame     = false;
    int  m_loadGeneration    = 0;
    qint64 m_frameStepMs     = 40;

    // --- Core media objects ---
    QMediaPlayer   *m_player      = nullptr;
    QAudioOutput   *m_audioOutput = nullptr;
    QVideoWidget   *m_videoWidget = nullptr;

    // --- UI ---
    TimelineWidget *m_timeline        = nullptr;
    ClickOverlay   *m_overlay         = nullptr;

    QLabel         *m_inTimeLabel     = nullptr;
    QLabel         *m_currentTimeLabel = nullptr;
    QLabel         *m_outTimeLabel    = nullptr;

    QPushButton    *m_btnGoToStart    = nullptr;
    QPushButton    *m_btnPrevFrame    = nullptr;
    QPushButton    *m_btnSetStart     = nullptr;
    QPushButton    *m_btnPlayPause    = nullptr;
    QPushButton    *m_btnSetEnd       = nullptr;
    QPushButton    *m_btnNextFrame    = nullptr;
    QPushButton    *m_btnStop         = nullptr;

    QPushButton    *m_btnLast30s      = nullptr;
    QPushButton    *m_btn30sHere      = nullptr;
    QPushButton    *m_btnFullClip     = nullptr;

    QSlider *m_volumeSlider = nullptr;
};

#endif // PLAYER_H
