#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>

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
    Ui::MainWindow *ui;
    QString inputFilePath;
    QString outputFilePath;
    QString ffmpegPath;
    QString ffprobePath;
    QProcess *ffmpegProcess;
    int totalDuration = 0;
    int getVideoDuration(const QString &filePath);
    bool initializeBinaryPaths();
};
#endif // MAINWINDOW_H
