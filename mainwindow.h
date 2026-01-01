#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include "videoinfo.h"

// Forward declaration
class Player;

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

private slots:
    void selectInputFile();
    void selectOutputFile();
    void startEncoding();
    void updateProgress();
    void showAboutDialog();
    void updateEstimatedFileSize();
    void updateMarkedDuration(qint64 startMs, qint64 endMs);

private:
    // -------- Helpers --------
    VideoInfo probeVideo(const QString &filePath);
    bool initializeBinaryPaths();
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

    VideoInfo m_sourceInfo;

    QString inputFilePath;
    QString outputFilePath;
    QString trimmedFilePath;
    QString ffmpegInputFile;
    QString ffmpegPath;
    QString ffprobePath;

    QProcess *ffmpegProcess = nullptr;

    bool m_userAdjustedVideoBitrate = false;
    bool isTrimming = false;

    qint64 videoDurationMs = 0;
    int totalDuration = 0;
};

#endif // MAINWINDOW_H
