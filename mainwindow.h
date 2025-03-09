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
    QString trimmedFilePath;
    QProcess *ffmpegProcess;
    int totalDuration = 0;
    QString ffmpegInputFile;
    int getVideoDuration(const QString &filePath);
    void deleteTrimmedFile(const QString &filePath);
};
#endif // MAINWINDOW_H
