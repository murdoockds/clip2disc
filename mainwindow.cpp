#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "videoinfo.h"
#include "player.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , ffmpegProcess(new QProcess(this))
{
    ui->setupUi(this);

    // 🔧 Ensure videoContainer has a layout
    if (!ui->videoContainer->layout()) {
        ui->videoContainer->setLayout(new QVBoxLayout);
        ui->videoContainer->layout()->setContentsMargins(0, 0, 0, 0);
    }

    // 🎬 Create the player INSIDE the container
    m_player = new Player(ui->videoContainer);
    ui->videoContainer->layout()->addWidget(m_player);

    // 🔥 THIS WAS THE MISSING LINK
    connect(m_player, &Player::requestOpenFile,
            this, &MainWindow::selectInputFile);

    qDebug() << "Application started";
    qDebug() << "App dir:" << QCoreApplication::applicationDirPath();

    connect(ui->inputButton, &QPushButton::clicked, this, &MainWindow::selectInputFile);
    connect(ui->outputButton, &QPushButton::clicked, this, &MainWindow::selectOutputFile);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startEncoding);
    connect(ui->aboutButton, &QPushButton::clicked, this, &MainWindow::showAboutDialog);
    connect(ffmpegProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::updateProgress);

    if (!initializeBinaryPaths()) {
        qDebug() << "FFmpeg initialization failed";
        ui->inputButton->setEnabled(false);
        ui->outputButton->setEnabled(false);
        ui->startButton->setEnabled(false);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

VideoInfo MainWindow::probeVideo(const QString &filePath)
{
    VideoInfo info;

    QProcess process;
    process.setProgram(ffprobePath);
    process.setArguments({
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        filePath
    });

    process.start();
    process.waitForFinished(3000);

    QByteArray output = process.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(output);

    if (!doc.isObject())
        return info;

    QJsonObject root = doc.object();

    // ---- format section ----
    QJsonObject format = root["format"].toObject();
    info.duration = format["duration"].toString().toDouble();
    info.bitrate  = format["bit_rate"].toString().toLongLong();

    // ---- streams section ----
    QJsonArray streams = root["streams"].toArray();
    for (const QJsonValue &v : streams) {
        QJsonObject s = v.toObject();
        QString type = s["codec_type"].toString();

        if (type == "video") {
            info.videoCodec = s["codec_name"].toString();
            info.width  = s["width"].toInt();
            info.height = s["height"].toInt();

            // FPS: r_frame_rate = "30000/1001"
            QString fpsStr = s["r_frame_rate"].toString();
            if (fpsStr.contains("/")) {
                auto parts = fpsStr.split("/");
                info.fps = parts[0].toDouble() / parts[1].toDouble();
            }
        }

        if (type == "audio") {
            info.audioCodec = s["codec_name"].toString();
        }
    }

    return info;
}


bool MainWindow::initializeBinaryPaths()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QDir binariesDir(appDir + "/binaries");

    QString ffmpegPath = binariesDir.absoluteFilePath("ffmpeg");
    QString ffprobePath = binariesDir.absoluteFilePath("ffprobe");

#ifdef Q_OS_WIN
    ffmpegPath += ".exe";
    ffprobePath += ".exe";
#endif

    qDebug() << "Looking for FFmpeg binaries";
    qDebug() << "ffmpeg candidate:" << ffmpegPath;
    qDebug() << "ffprobe candidate:" << ffprobePath;

    bool localBinariesExist = QFile::exists(ffmpegPath) && QFile::exists(ffprobePath);

    if (localBinariesExist) {
        this->ffmpegPath = ffmpegPath;
        this->ffprobePath = ffprobePath;
        qDebug() << "Using local FFmpeg binaries";
        return true;
    }

    qDebug() << "Local binaries not found, testing system FFmpeg";

    QProcess testProcess;
    testProcess.start("ffmpeg", {"-version"});
    bool systemFfmpegExists = testProcess.waitForFinished(3000) && testProcess.exitCode() == 0;

    testProcess.start("ffprobe", {"-version"});
    bool systemFfprobeExists = testProcess.waitForFinished(3000) && testProcess.exitCode() == 0;

    qDebug() << "System ffmpeg:" << systemFfmpegExists;
    qDebug() << "System ffprobe:" << systemFfprobeExists;

    if (systemFfmpegExists && systemFfprobeExists) {
        this->ffmpegPath = "ffmpeg";
        this->ffprobePath = "ffprobe";
        return true;
    }

    QMessageBox::critical(this, "Error", "FFmpeg binaries not found!");
    return false;
}

void MainWindow::selectInputFile()
{
    inputFilePath = QFileDialog::getOpenFileName(
        this,
        "Select Video File",
        "",
        "Videos (*.mp4 *.mkv *.avi *.mov)"
    );

    if (inputFilePath.isEmpty())
        return;

    ui->inputLabel->setPlainText(inputFilePath);

    VideoInfo info = probeVideo(inputFilePath);

    // Example: update UI labels
    ui->durationLabel->setText(
        QString::number(info.duration, 'f', 1) + " s");

    ui->resolutionLabel->setText(
        QString("%1x%2").arg(info.width).arg(info.height));

    ui->codecLabel->setText(
        QString("V: %1 | A: %2")
            .arg(info.videoCodec)
            .arg(info.audioCodec));

    ui->fpsLabel->setText(
        QString::number(info.fps, 'f', 2) + " fps");

    qDebug() << "Probed video:"
             << info.duration
             << info.width << "x" << info.height
             << info.videoCodec
             << info.audioCodec
             << info.fps;

    QFileInfo inputInfo(inputFilePath);

    outputFilePath =
        inputInfo.absolutePath() + "/" +
        inputInfo.completeBaseName() +
        "-clipped.mp4";

    ui->outputLabel->setPlainText(outputFilePath);

    // 🔥 This is the key line
    m_player->setSourceFile(inputFilePath);

    qDebug() << "Auto-filled output file:" << outputFilePath;
}


void MainWindow::selectOutputFile()
{
    outputFilePath = QFileDialog::getSaveFileName(this, "Select Output File", "", "MP4 Files (*.mp4)");
    if (!outputFilePath.isEmpty())
        ui->outputLabel->setPlainText(outputFilePath);
}

void MainWindow::startEncoding()
{
    qDebug() << "Start encoding clicked";
    qDebug() << "Input:" << inputFilePath;
    qDebug() << "Output:" << outputFilePath;

    if (inputFilePath.isEmpty() || outputFilePath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select both input and output files.");
        return;
    }

    if (!outputFilePath.endsWith(".mp4", Qt::CaseInsensitive)) {
        outputFilePath += ".mp4";
        ui->outputLabel->setPlainText(outputFilePath);
    }

    ui->inputButton->setEnabled(false);
    ui->outputButton->setEnabled(false);
    ui->startButton->setEnabled(false);
    ui->progressBar->setValue(0);

    int videoDuration = getVideoDuration(inputFilePath);
    qDebug() << "Input duration:" << videoDuration << "seconds";

    bool shouldTrim = (videoDuration > 30);
    isTrimming = shouldTrim;

    trimmedFilePath.clear();
    ffmpegInputFile = inputFilePath;

    if (shouldTrim) {
        totalDuration = 30;

        QFileInfo inputInfo(outputFilePath);

        trimmedFilePath =
            inputInfo.absolutePath() + "/" +
            inputInfo.completeBaseName() +
            "-trimming.mp4";


        QStringList trimArgs = {
            "-y",
            "-ss", QString::number(videoDuration - 30),
            "-i", inputFilePath,
            "-t", "30",
            "-c:v", "libx264",
            "-preset", "veryfast",
            "-crf", "18",
            "-c:a", "aac",
            "-movflags", "+faststart",
            "-progress", "pipe:1",
            "-nostats",
            "-loglevel", "error",
            trimmedFilePath
        };

        qDebug() << "Running trim FFmpeg:";
        qDebug() << ffmpegPath << trimArgs;

        QProcess trimProcess;
        trimProcess.setProgram(ffmpegPath);
        trimProcess.setArguments(trimArgs);
        trimProcess.setProcessChannelMode(QProcess::MergedChannels);
        trimProcess.start();

        while (trimProcess.state() != QProcess::NotRunning) {
            trimProcess.waitForReadyRead(100);
            ffmpegProcess = &trimProcess;
            updateProgress();
            QCoreApplication::processEvents();
        }

        qDebug() << "Trim exit code:" << trimProcess.exitCode();

        isTrimming = false;

        totalDuration = getVideoDuration(trimmedFilePath);
        qDebug() << "Trimmed duration:" << totalDuration;

        ffmpegInputFile = trimmedFilePath;
    } else {
        totalDuration = videoDuration;
        isTrimming = false;
    }

    qDebug() << "Starting compression";
    qDebug() << "Input used:" << ffmpegInputFile;

    ffmpegProcess = new QProcess(this);

    connect(ffmpegProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::updateProgress);

    connect(ffmpegProcess, &QProcess::finished,
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {

        qDebug() << "Compression finished";
        qDebug() << "Exit code:" << exitCode;
        qDebug() << "Exit status:" << exitStatus;

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            deleteTrimmedFile(trimmedFilePath);
        }
    });

    QStringList compressArgs = {
        "-y",
        "-i", ffmpegInputFile,
        "-c:v", "libx264",
        "-preset", "fast",
        "-b:v", "2550k",
        "-r", "48",
        "-vf", "scale=1280:720",
        "-movflags", "+faststart",
        "-progress", "pipe:1",
        "-nostats",
        "-loglevel", "error",
        outputFilePath
    };

    qDebug() << "Compression command:";
    qDebug() << ffmpegPath << compressArgs;

    ffmpegProcess->setProgram(ffmpegPath);
    ffmpegProcess->setArguments(compressArgs);
    ffmpegProcess->setProcessChannelMode(QProcess::MergedChannels);
    ffmpegProcess->start();

    qDebug() << "FFmpeg compression started";
}

void MainWindow::deleteTrimmedFile(const QString &trimmedFilePath)
{
    if (!trimmedFilePath.isEmpty() && QFile::exists(trimmedFilePath)) {
        qDebug() << "Deleting trimmed file:" << trimmedFilePath;
        QFile::remove(trimmedFilePath);
    }
}

int MainWindow::getVideoDuration(const QString &filePath)
{
    qDebug() << "Getting duration for:" << filePath;

    QProcess process;
    process.setProgram(ffprobePath);
    process.setArguments({
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        filePath
    });

    process.start();

    if (!process.waitForFinished(3000)) {
        qDebug() << "ffprobe timeout";
        return 0;
    }

    QString output = process.readAllStandardOutput().trimmed();
    qDebug() << "ffprobe output:" << output;

    bool ok;
    int duration = output.toDouble(&ok);
    return ok ? duration : 0;
}

void MainWindow::updateProgress()
{
    QString text = ffmpegProcess->readAllStandardOutput();
    qDebug() << "FFmpeg progress:" << text;

    static const QRegularExpression re("out_time_ms=([0-9]+)");
    QRegularExpressionMatch match = re.match(text);

    if (match.hasMatch() && totalDuration > 0) {
        qint64 currentMs = match.captured(1).toLongLong();
        int currentSeconds = currentMs / 1000000;

        int percent = qMin((currentSeconds * 100) / totalDuration, 100);

        if (isTrimming) {
            percent = percent * 40 / 100;        // 0–40%
        } else {
            percent = 40 + percent * 60 / 100;   // 40–100%
        }

        ui->progressBar->setValue(percent);
    }

    // ✅ Only finish when NOT trimming
    if (text.contains("progress=end") && !isTrimming) {
        qDebug() << "Compression reported progress=end";

        ui->progressBar->setValue(100);
        ui->inputButton->setEnabled(true);
        ui->outputButton->setEnabled(true);
        ui->startButton->setEnabled(true);

        QMessageBox::information(this, "Finished", "Video compressed!");
    }
}


void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, "About",
                       "<p><b>Clip2Disc</b></p>"
                       "<p>A wrapper for FFmpeg video compressor that reduces video size to under 10MB, allowing them to be sent in Discord chats.</p>"
                       "<p><a href='https://github.com/murdoockds/clip2disc'>GitHub Repository</a></p>");
}
