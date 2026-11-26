#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include "videoinfo.h"

// Forward declarations
class Player;
class QComboBox;
class QPushButton;
class QLabel;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void selectInputFile();
    void loadInputFile(const QString &filePath);
    void selectOutputFile();
    void startEncoding();
    void updateProgress();
    void onEncodingErrorOutput();
    void onEncodingFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onEncodingProcessError(QProcess::ProcessError error);
    void onPlaybackFailed(const QString &reason);
    void onProxyFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void fitToDiscordLimit(double targetSizeMB);
    void showAboutDialog();
    void updateEstimatedFileSize();
    void updateMarkedDuration(qint64 startMs, qint64 endMs);

private:
    // -------- Helpers --------
    void applyModernTheme();
    void setupShortcuts();
    VideoInfo probeVideo(const QString &filePath);
    bool initializeBinaryPaths();
    void detectAvailableEncoders();
    bool testVideoEncoder(const QString &encoderName);
    void generatePreviewProxy(bool useHardware);
    void cleanupPreviewProxy();
    void setEncodingUiBusy(bool busy);
    void deleteTrimmedFile(const QString &filePath);
    int getVideoDuration(const QString &filePath);

    void autoAdjustVideoBitrateForResolution();

    int computeScaledVideoBitrate(int userBitrate,
                                  int outWidth,
                                  int outHeight,
                                  int fps) const;

    // -------- State --------
    Ui::MainWindow *ui = nullptr;
    Player *m_player = nullptr;
    QComboBox *m_encoderCombo = nullptr;
    QPushButton *m_fit10MbButton = nullptr;
    QPushButton *m_fit20MbButton = nullptr;
    QLabel *m_estimateHintLabel = nullptr;

    VideoInfo m_sourceInfo;

    QString inputFilePath;
    QString outputFilePath;
    QString trimmedFilePath;
    QString ffmpegInputFile;
    QString ffmpegPath;
    QString ffprobePath;
    QString m_previewProxyPath;
    QString m_ffmpegErrorLog;

    QProcess *ffmpegProcess = nullptr;
    QProcess *m_proxyProcess = nullptr;

    bool m_userAdjustedVideoBitrate = false;
    bool m_hasH264Nvenc = false;
    bool m_hasAv1Nvenc = false;
    bool m_proxyAttempted = false;
    bool m_proxyUsedHw = false;
    bool isTrimming = false;

    qint64 videoDurationMs = 0;
    double totalDuration = 0.0;
};

#endif // MAINWINDOW_H
