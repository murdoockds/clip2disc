#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include "videoinfo.h"

// Forward declaration (IMPORTANT)
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
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void selectInputFile();
    void selectOutputFile();
    void startEncoding();
    void updateProgress();
    void showAboutDialog();
    
private:
    VideoInfo probeVideo(const QString &filePath);

    Ui::MainWindow *ui;

    // 🔽 Embedded media player
    Player *m_player = nullptr;

    // Existing fields
    QString inputFilePath;
    QString outputFilePath;
    QString trimmedFilePath;
    QString ffmpegInputFile;
    QString ffmpegPath;
    QString ffprobePath;
    QProcess *ffmpegProcess;
    qint64 videoDurationMs = 0;
    int totalDuration = 0;

    int getVideoDuration(const QString &filePath);
    void deleteTrimmedFile(const QString &filePath);
    bool initializeBinaryPaths();
    bool isTrimming = false;
};
#endif // MAINWINDOW_H
