#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , ffmpegProcess(new QProcess(this))
{
    ui->setupUi(this);
    connect(ui->inputButton, &QPushButton::clicked, this, &MainWindow::selectInputFile);
    connect(ui->outputButton, &QPushButton::clicked, this, &MainWindow::selectOutputFile);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startEncoding);
    connect(ui->aboutButton, &QPushButton::clicked, this, &MainWindow::showAboutDialog);
    connect(ffmpegProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::updateProgress);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::selectInputFile()
{
    inputFilePath = QFileDialog::getOpenFileName(this, "Select Video File", "", "Videos (*.mp4 *.mkv *.avi *.mov)");
    if (!inputFilePath.isEmpty())
        ui->inputLabel->setPlainText(inputFilePath);
}

void MainWindow::selectOutputFile()
{
    outputFilePath = QFileDialog::getSaveFileName(this, "Select Output File", "", "MP4 Files (*.mp4)");
    if (!outputFilePath.isEmpty())
        ui->outputLabel->setPlainText(outputFilePath);
}

void MainWindow::startEncoding()
{
    if (inputFilePath.isEmpty() || outputFilePath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select both input and output files.");
        return;
    }

    ui->inputButton->setEnabled(false);
    ui->outputButton->setEnabled(false);
    ui->startButton->setEnabled(false);

    ui->progressBar->setValue(0);

    int videoDuration = getVideoDuration(inputFilePath);
    bool shouldTrim = (videoDuration > 30);
    trimmedFilePath.clear();
    ffmpegInputFile = inputFilePath;

    if (shouldTrim) {
        trimmedFilePath = QFileInfo(outputFilePath).absolutePath() + "/for_compression_trimmed_video.mp4";
        QStringList trimArgs = {"-y",
                                "-i", inputFilePath,
                                "-ss", QString::number(videoDuration - 30),
                                "-t", "30",
                                "-c:v", "copy",
                                "-c:a", "copy",
                                trimmedFilePath};

        QProcess trimProcess;
        trimProcess.start("ffmpeg", trimArgs);
        if (!trimProcess.waitForFinished(-1)) {
            QMessageBox::warning(this, "Error", "Trimming process failed.");
            return;
        }
        totalDuration = getVideoDuration(trimmedFilePath);
        ffmpegInputFile = trimmedFilePath;
    } else {
        totalDuration = videoDuration;
    }

    ffmpegProcess = new QProcess(this);
    connect(ffmpegProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::updateProgress);
    connect(ffmpegProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            deleteTrimmedFile(trimmedFilePath);
        }
    });

    QStringList compressArgs = {"-y",
                                "-i", ffmpegInputFile,
                                "-c:v", "libx264",
                                "-preset", "veryfast",
                                "-b:v", "2600k",
                                "-r", "48",
                                "-vf", "scale=1280:720",
                                "-movflags", "+faststart",
                                "-progress", "pipe:1",
                                "-nostats",
                                "-loglevel", "error",
                                outputFilePath};
    ffmpegProcess->setProgram("ffmpeg");
    ffmpegProcess->setArguments(compressArgs);
    ffmpegProcess->setProcessChannelMode(QProcess::MergedChannels);
    ffmpegProcess->setReadChannel(QProcess::StandardOutput);
    ffmpegProcess->start();
}

void MainWindow::deleteTrimmedFile(const QString &trimmedfilePath)
{
    if (!trimmedFilePath.isEmpty() && QFile::exists(trimmedFilePath)) {
        QFile::remove(trimmedFilePath);
    }
}

int MainWindow::getVideoDuration(const QString &filePath)
{
    QProcess process;
    process.start("ffprobe", {"-v", "error",
                              "-select_streams", "v:0",
                              "-show_entries", "format=duration",
                              "-of", "default=noprint_wrappers=1:nokey=1",
                              filePath});
    if (!process.waitForFinished(3000)) return 0;

    bool ok;
    int duration = process.readAllStandardOutput().trimmed().toDouble(&ok);
    return ok ? duration : 0;
}

void MainWindow::updateProgress()
{
    static const QRegularExpression re("out_time=([0-9]+):([0-9]+):([0-9]+\\.?[0-9]*)");

    QString text = ffmpegProcess->readAllStandardOutput();
    QRegularExpressionMatch match = re.match(text);

    if (match.hasMatch()) {
        int currentSeconds = match.captured(1).toInt() * 3600 + match.captured(2).toInt() * 60 + match.captured(3).toDouble();
        if (totalDuration > 0) {
            int progress = qMin(static_cast<int>((currentSeconds * 100) / totalDuration), 100);
            QMetaObject::invokeMethod(this, [=]() { ui->progressBar->setValue(progress); }, Qt::QueuedConnection);
        }
    }

    if (text.contains("progress=end")) {
        ffmpegProcess->disconnect();
        QMetaObject::invokeMethod(this, [=]() {
            ui->progressBar->setValue(100);
            ui->inputButton->setEnabled(true);
            ui->outputButton->setEnabled(true);
            ui->startButton->setEnabled(true);
        }, Qt::QueuedConnection);

        QMessageBox::information(this, "Finished", "Video compressed!");
        deleteTrimmedFile(trimmedFilePath);
    }
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, "About",
                       "<p><b>Clip2Disc</b></p>"
                       "<p>A wrapper for FFmpeg video compressor that reduces video size to under 10MB, allowing them to be sent in Discord chats.</p>"
                       "<p><a href='https://github.com/murdoockds/clip2disc'>GitHub Repository</a></p>");
}
