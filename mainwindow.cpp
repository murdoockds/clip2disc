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

    connect(ui->videoBitrateSlider, &QSlider::valueChanged,
            this, [this](int value) {

                m_userAdjustedVideoBitrate = true;

                int percent = (value * 100) / m_sourceInfo.videoBitrate;

                ui->videoBitrateLabel->setText(
                    QString("Bitrate: %1 kbps (%2%)")
                        .arg(value)
                        .arg(percent));

                updateEstimatedFileSize();
            });


    ui->videoBitrateSlider->setSingleStep(100);
    ui->videoBitrateSlider->setPageStep(500);

    connect(ui->fpsSlider, &QSlider::valueChanged,
            this, [this](int value) {

                ui->fpsLabel->setText(
                    QString("FPS: %1").arg(value));

                updateEstimatedFileSize();
            });

    ui->fpsSlider->setEnabled(false);

    connect(ui->audioBitrateSlider, &QSlider::valueChanged,
            this, [this](int value) {

                if (m_sourceInfo.audioBitrate <= 0)
                    return;

                int percent =
                    (value * 100) / m_sourceInfo.audioBitrate;

                ui->audioBitrateLabel->setText(
                    QString("Audio bitrate: %1 kbps (%2%)")
                        .arg(value)
                        .arg(percent));

                updateEstimatedFileSize();
            });


    ui->audioBitrateSlider->setSingleStep(16);
    ui->audioBitrateSlider->setPageStep(64);
    ui->audioBitrateSlider->setEnabled(false);

    connect(ui->resolutionCombo, &QComboBox::currentIndexChanged,
            this, [this](int) {

                if (!m_userAdjustedVideoBitrate)
                    autoAdjustVideoBitrateForResolution();

                updateEstimatedFileSize();
            });


    ui->resolutionCombo->setEnabled(false);

    connect(m_player, &Player::trimChanged,
            this, &MainWindow::updateMarkedDuration);

    connect(m_player, &Player::trimChanged,
            this, [this](qint64, qint64) {
                updateEstimatedFileSize();
            });

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
    if (!process.waitForFinished(5000))
        return info;

    const QByteArray output = process.readAllStandardOutput();
    const QJsonDocument doc = QJsonDocument::fromJson(output);

    if (!doc.isObject())
        return info;

    const QJsonObject root = doc.object();

    // ---------- FORMAT (container) ----------
    const QJsonObject format = root["format"].toObject();

    info.duration = format["duration"].toString().toDouble();

    // container bitrate (bits/sec → kbps)
    if (format.contains("bit_rate")) {
        info.bitrate = format["bit_rate"].toString().toLongLong() / 1000;
    }

    // ---------- STREAMS ----------
    const QJsonArray streams = root["streams"].toArray();

    for (const QJsonValue &v : streams) {
        const QJsonObject s = v.toObject();
        const QString type = s["codec_type"].toString();

        if (type == "video") {
            info.videoCodec = s["codec_name"].toString();
            info.width  = s["width"].toInt();
            info.height = s["height"].toInt();

            // FPS (e.g. "30000/1001")
            const QString fpsStr = s["r_frame_rate"].toString();
            if (fpsStr.contains("/")) {
                const auto parts = fpsStr.split("/");
                const double num = parts.value(0).toDouble();
                const double den = parts.value(1).toDouble();
                if (den != 0.0)
                    info.fps = num / den;
            }

            // video bitrate (bits/sec → kbps)
            if (s.contains("bit_rate")) {
                info.videoBitrate =
                    s["bit_rate"].toString().toLongLong() / 1000;
            }
        }
        else if (type == "audio") {
            info.audioCodec = s["codec_name"].toString();

            // audio bitrate (bits/sec → kbps)
            if (s.contains("bit_rate")) {
                info.audioBitrate =
                    s["bit_rate"].toString().toLongLong() / 1000;
            }
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

void MainWindow::updateEstimatedFileSize()
{
    if (!m_player || m_sourceInfo.duration <= 0) {
        ui->fileSizeLabel->setText("—");
        return;
    }

    // --- Resolution ---
    QString res = ui->resolutionCombo->currentData().toString();
    int outW = m_sourceInfo.width;
    int outH = m_sourceInfo.height;

    QStringList parts = res.split("x");
    if (parts.size() == 2) {
        outW = parts[0].toInt();
        outH = parts[1].toInt();
    }

    // --- FPS ---
    int fps = ui->fpsSlider->isEnabled()
                  ? ui->fpsSlider->value()
                  : int(m_sourceInfo.fps);

    // --- Video bitrate (scaled) ---
    int userVideoBitrate = ui->videoBitrateSlider->value();
    int videoBitrate =
        computeScaledVideoBitrate(userVideoBitrate, outW, outH, fps);

    // --- Audio bitrate ---
    int audioBitrate = ui->audioBitrateSlider->isEnabled()
                           ? ui->audioBitrateSlider->value()
                           : m_sourceInfo.audioBitrate;

    double totalBitrateKbps = videoBitrate + audioBitrate;
    if (totalBitrateKbps <= 0) {
        ui->fileSizeLabel->setText("—");
        return;
    }

    // --- Duration (trim-aware) ---
    double durationSec = m_sourceInfo.duration;
    qint64 startMs = m_player->trimStart();
    qint64 endMs   = m_player->trimEnd();

    if (endMs > startMs)
        durationSec = (endMs - startMs) / 1000.0;

    double sizeMB =
        (totalBitrateKbps * durationSec) / (8.0 * 1024.0);

    ui->fileSizeLabel->setText(
        QString("File size: ~%1 MB")
            .arg(sizeMB, 0, 'f', 1)
        );
}

void MainWindow::autoAdjustVideoBitrateForResolution()
{
    if (m_sourceInfo.videoBitrate <= 0)
        return;

    QString res = ui->resolutionCombo->currentData().toString();
    if (res.isEmpty())
        return;

    QStringList parts = res.split("x");
    if (parts.size() != 2)
        return;

    int rw = parts[0].toInt();
    int rh = parts[1].toInt();

    double originalPixels =
        double(m_sourceInfo.width) * m_sourceInfo.height;
    double targetPixels =
        double(rw) * rh;

    if (originalPixels <= 0)
        return;

    // --- Pixel-based scaling (gentle curve) ---
    double scale = targetPixels / originalPixels;
    scale = std::pow(scale, 0.85);
    scale = qBound(0.30, scale, 1.0);

    int newBitrate =
        int(m_sourceInfo.videoBitrate * scale);

    ui->videoBitrateSlider->blockSignals(true);
    ui->videoBitrateSlider->setValue(newBitrate);
    ui->videoBitrateSlider->blockSignals(false);

    ui->videoBitrateLabel->setText(
        QString("Bitrate: %1 kbps (auto)")
            .arg(newBitrate));
}


void MainWindow::selectInputFile()
{
    m_userAdjustedVideoBitrate = false;

    inputFilePath = QFileDialog::getOpenFileName(
        this,
        "Select Video File",
        "",
        "Videos (*.mp4 *.mkv *.avi *.mov)"
        );

    if (inputFilePath.isEmpty())
        return;

    ui->inputLabel->setPlainText(inputFilePath);

    m_sourceInfo = probeVideo(inputFilePath);

    // ---- Static info ----
    ui->durationLabel->setText(
        QString::number(m_sourceInfo.duration, 'f', 1) + " s");

    //ui->resolutionLabel->setText(
    //    QString("%1x%2")
    //        .arg(m_sourceInfo.width)
    //        .arg(m_sourceInfo.height));

    ui->codecLabel->setText(
        QString("V: %1 | A: %2")
            .arg(m_sourceInfo.videoCodec)
            .arg(m_sourceInfo.audioCodec));

    ui->fpsLabel->setText(
        QString::number(m_sourceInfo.fps, 'f', 2) + " fps");

    // ---- Video bitrate ----
    const int maxVideoBitrate =
        qMax<qint64>(1, m_sourceInfo.videoBitrate);

    ui->videoBitrateSlider->blockSignals(true);

    ui->videoBitrateSlider->setEnabled(true);
    ui->videoBitrateSlider->setMinimum(1);
    ui->videoBitrateSlider->setMaximum(maxVideoBitrate);
    ui->videoBitrateSlider->setValue(maxVideoBitrate);

    ui->videoBitrateSlider->blockSignals(false);


    ui->videoBitrateLabel->setText(
        QString("Bitrate: %1 kbps").arg(maxVideoBitrate));

    // ---- FPS ----
    const int maxFps = qMax(1, int(m_sourceInfo.fps));

    ui->fpsSlider->setEnabled(true);
    ui->fpsSlider->setMinimum(1);
    ui->fpsSlider->setMaximum(maxFps);
    ui->fpsSlider->setValue(maxFps);

    ui->fpsLabel->setText(
        QString("FPS: %1").arg(maxFps));

    // ---- Audio bitrate ----
    const int maxAudioBitrate =
        qMax<qint64>(32, m_sourceInfo.audioBitrate);

    ui->audioBitrateSlider->setEnabled(true);
    ui->audioBitrateSlider->setMinimum(32);
    ui->audioBitrateSlider->setMaximum(maxAudioBitrate);
    ui->audioBitrateSlider->setValue(maxAudioBitrate);

    ui->audioBitrateLabel->setText(
        QString("Audio bitrate: %1 kbps").arg(maxAudioBitrate));

    // ---- Resolution ----
    ui->resolutionCombo->clear();

    int w = m_sourceInfo.width;
    int h = m_sourceInfo.height;

    // Helper: avoid duplicates using itemData (not visible text)
    auto addIfMissing = [&](const QString &resValue, const QString &resLabel) {
        for (int i = 0; i < ui->resolutionCombo->count(); ++i) {
            if (ui->resolutionCombo->itemData(i).toString() == resValue)
                return;
        }
        ui->resolutionCombo->addItem(resLabel, resValue);
    };

    // Original resolution (pretty label, clean data)
    QString originalRes = QString("%1x%2").arg(w).arg(h);
    addIfMissing(originalRes, originalRes + " (Original)");

    // Common downscale options
    if (w >= 1920 && h >= 1080)
        addIfMissing("1920x1080", "1920x1080");

    if (w >= 1280 && h >= 720)
        addIfMissing("1280x720", "1280x720");

    if (w >= 854 && h >= 480)
        addIfMissing("854x480", "854x480");

    if (w >= 640 && h >= 360)
        addIfMissing("640x360", "640x360");

    // Default to original
    ui->resolutionCombo->setCurrentIndex(0);
    ui->resolutionCombo->setEnabled(true);

    updateEstimatedFileSize();

    // ---- Output path ----
    QFileInfo inputInfo(inputFilePath);
    outputFilePath =
        inputInfo.absolutePath() + "/" +
        inputInfo.completeBaseName() +
        "-clipped.mp4";

    ui->outputLabel->setPlainText(outputFilePath);

    // ---- Player ----
    m_player->setSourceFile(inputFilePath);

    updateMarkedDuration(0, m_sourceInfo.duration * 1000);

}

void MainWindow::selectOutputFile()
{
    outputFilePath = QFileDialog::getSaveFileName(this, "Select Output File", "", "MP4 Files (*.mp4)");
    if (!outputFilePath.isEmpty())
        ui->outputLabel->setPlainText(outputFilePath);
}

void MainWindow::updateMarkedDuration(qint64 startMs, qint64 endMs)
{
    if (m_sourceInfo.duration <= 0)
        return;

    const double markedSec =
        qMax<qint64>(0, endMs - startMs) / 1000.0;

    const double originalSec = m_sourceInfo.duration;

    ui->durationLabel->setText(
        QString("%1 s / %2 s")
            .arg(markedSec, 0, 'f', 1)
            .arg(originalSec, 0, 'f', 1)
        );
}



void MainWindow::startEncoding()
{
    qDebug() << "Start encoding clicked";

    if (inputFilePath.isEmpty() || outputFilePath.isEmpty()) {
        QMessageBox::warning(this, "Warning",
                             "Please select both input and output files.");
        return;
    }

    m_player->pause();

    // --- Trim ---
    qint64 trimStartMs = m_player->trimStart();
    qint64 trimEndMs   = m_player->trimEnd();

    bool hasTrim =
        trimEndMs > trimStartMs &&
        trimEndMs <= m_sourceInfo.duration * 1000;

    double trimStartSec = trimStartMs / 1000.0;
    double trimDurationSec =
        (trimEndMs - trimStartMs) / 1000.0;

    // --- UI values ---
    int userVideoBitrate = ui->videoBitrateSlider->value();
    int audioBitrate     = ui->audioBitrateSlider->value();
    int fps              = ui->fpsSlider->value();

    QString res = ui->resolutionCombo->currentData().toString();
    int outW = m_sourceInfo.width;
    int outH = m_sourceInfo.height;

    QStringList parts = res.split("x");
    if (parts.size() == 2) {
        outW = parts[0].toInt();
        outH = parts[1].toInt();
    }

    // --- Scaled video bitrate ---
    int videoBitrate =
        computeScaledVideoBitrate(userVideoBitrate, outW, outH, fps);
    QString videoBitrateArg = QString::number(videoBitrate) + "k";
    QString fpsArg = QString::number(fps);
    QString scaleFilter = QString("scale=%1:%2:flags=lanczos")
                              .arg(outW / 2 * 2)
                              .arg(outH / 2 * 2);

    // --- CRF tuning ---
    int crf = 22;
    if (outH <= 720) crf = 23;
    if (outH <= 480) crf = 24;

    QStringList args;
    args << "-y";

    if (hasTrim) {
        args << "-ss" << QString::number(trimStartSec, 'f', 3)
        << "-t"  << QString::number(trimDurationSec, 'f', 3);
        totalDuration = trimDurationSec;
    } else {
        totalDuration = m_sourceInfo.duration;
    }

    args << "-i" << inputFilePath;

    // --- Video ---
    args << "-c:v" << "libx264"
         << "-preset" << "fast"
         << "-b:v" << videoBitrateArg
         << "-maxrate" << videoBitrateArg
         << "-bufsize" << QString::number(videoBitrate * 2) + "k"
         << "-r" << fpsArg
         << "-vf" << scaleFilter;

    // --- Audio ---
    args << "-c:a" << "aac"
         << "-b:a" << QString::number(audioBitrate) + "k";

    // --- MP4 + progress ---
    args << "-movflags" << "+faststart"
         << "-progress" << "pipe:1"
         << "-nostats"
         << "-loglevel" << "error";

    args << outputFilePath;

    qDebug() << "FFmpeg:" << ffmpegPath << args;

    ffmpegProcess->setProgram(ffmpegPath);
    ffmpegProcess->setArguments(args);
    ffmpegProcess->setProcessChannelMode(QProcess::MergedChannels);
    ffmpegProcess->start();
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

int MainWindow::computeScaledVideoBitrate(int userBitrate,
                                          int outWidth,
                                          int outHeight,
                                          int fps) const
{
    if (userBitrate <= 0)
        return 0;

    // Reference: 1080p @ 60fps
    const double refPixels = 1920.0 * 1080.0;
    const double curPixels = double(outWidth) * outHeight;

    double resFactor = curPixels / refPixels;
    resFactor = qBound(0.15, resFactor, 1.0);

    double fpsFactor = fps / 60.0;
    fpsFactor = qBound(0.35, fpsFactor, 1.0);

    int scaled =
        int(userBitrate * resFactor * fpsFactor);

    // Safety clamp
    scaled = qBound(400, scaled, userBitrate);

    return scaled;
}



void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, "About",
                       "<p><b>Clip2Disc</b></p>"
                       "<p>A wrapper for FFmpeg video compressor that reduces video size to under 10MB, allowing them to be sent in Discord chats.</p>"
                       "<p><a href='https://github.com/murdoockds/clip2disc'>GitHub Repository</a></p>");
}
