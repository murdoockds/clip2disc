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
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QShortcut>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <cmath>

namespace {

double parseRationalFps(const QString &fpsStr)
{
    if (!fpsStr.contains('/'))
        return 0.0;
    const auto parts = fpsStr.split('/');
    const double num = parts.value(0).toDouble();
    const double den = parts.value(1).toDouble();
    if (den <= 0.0)
        return 0.0;
    return num / den;
}

qint64 parseBitrateKbps(const QJsonObject &obj)
{
    if (obj.contains("bit_rate")) {
        bool ok = false;
        const qint64 bps = obj["bit_rate"].toString().toLongLong(&ok);
        if (ok && bps > 0)
            return bps / 1000;
    }

    // MKV / WebM streams frequently store bitrate in tags.BPS instead of bit_rate
    if (obj.contains("tags")) {
        const QJsonObject tags = obj["tags"].toObject();
        for (const QString &key : {QStringLiteral("BPS"), QStringLiteral("BPS-eng")}) {
            if (tags.contains(key)) {
                bool ok = false;
                const qint64 bps = tags[key].toString().toLongLong(&ok);
                if (ok && bps > 0)
                    return bps / 1000;
            }
        }
    }

    return 0;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , ffmpegProcess(new QProcess(this))
    , m_proxyProcess(new QProcess(this))
{
    ui->setupUi(this);
    setAcceptDrops(true);
    setWindowTitle("Clip2Disc");

    // Let the player region expand cleanly while header & footer stay compact
    ui->verticalLayout->setStretch(0, 0);
    ui->verticalLayout->setStretch(1, 1);
    ui->verticalLayout->setStretch(2, 0);
    ui->verticalLayout->setSpacing(8);

    // Give the right sidebar enough room for codec, encoder, and Discord fit controls
    ui->videoDetails->setMinimumWidth(235);
    ui->videoDetails->setMaximumWidth(260);
    ui->videoDetails->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    ui->verticalLayout_3->setSpacing(6);
    ui->verticalLayout_3->setContentsMargins(12, 12, 12, 12);

    ui->inputLabel->setFixedHeight(30);
    ui->outputLabel->setFixedHeight(30);
    ui->progressBar->setFixedHeight(26);
    ui->progressBar->setTextVisible(true);
    ui->startButton->setMinimumWidth(110);
    ui->startButton->setFixedHeight(30);
    ui->inputButton->setFixedHeight(30);
    ui->outputButton->setFixedHeight(30);

    // Add Encoder selector + Discord Auto-fit buttons to sidebar
    auto *encoderLabel = new QLabel("Encoder", ui->videoDetails);
    m_encoderCombo = new QComboBox(ui->videoDetails);
    m_encoderCombo->setEnabled(false);

    auto *fitLabel = new QLabel("Auto-fit Discord", ui->videoDetails);
    auto *fitRow = new QHBoxLayout();
    fitRow->setSpacing(6);

    m_fit10MbButton = new QPushButton("<10 MB", ui->videoDetails);
    m_fit10MbButton->setObjectName("fitButton");
    m_fit10MbButton->setToolTip("Automatically adjust bitrate to fit under 10 MB (~9.3 MB)");
    m_fit10MbButton->setEnabled(false);

    m_fit20MbButton = new QPushButton("<20 MB", ui->videoDetails);
    m_fit20MbButton->setObjectName("fitButton");
    m_fit20MbButton->setToolTip("Automatically adjust bitrate to fit under 20 MB (~18.8 MB)");
    m_fit20MbButton->setEnabled(false);

    fitRow->addWidget(m_fit10MbButton);
    fitRow->addWidget(m_fit20MbButton);

    m_estimateHintLabel = new QLabel("Select a video to estimate size", ui->videoDetails);
    m_estimateHintLabel->setWordWrap(true);
    m_estimateHintLabel->setStyleSheet("color: #949BA4; font-size: 11px;");

    ui->verticalLayout_3->addWidget(m_estimateHintLabel);
    ui->verticalLayout_3->addWidget(encoderLabel);
    ui->verticalLayout_3->addWidget(m_encoderCombo);
    ui->verticalLayout_3->addSpacing(4);
    ui->verticalLayout_3->addWidget(fitLabel);
    ui->verticalLayout_3->addLayout(fitRow);
    ui->verticalLayout_3->addStretch();

    // Ensure videoContainer has a layout
    if (!ui->videoContainer->layout()) {
        ui->videoContainer->setLayout(new QVBoxLayout);
        ui->videoContainer->layout()->setContentsMargins(0, 0, 0, 0);
    }

    // Create the player INSIDE the container
    m_player = new Player(ui->videoContainer);
    ui->videoContainer->layout()->addWidget(m_player);

    applyModernTheme();
    setupShortcuts();

    connect(m_player, &Player::requestOpenFile,
            this, &MainWindow::selectInputFile);
    connect(m_player, &Player::playbackFailed,
            this, &MainWindow::onPlaybackFailed);

    qDebug() << "Application started";
    qDebug() << "App dir:" << QCoreApplication::applicationDirPath();

    connect(ui->inputButton, &QPushButton::clicked, this, &MainWindow::selectInputFile);
    connect(ui->outputButton, &QPushButton::clicked, this, &MainWindow::selectOutputFile);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startEncoding);
    connect(ui->aboutButton, &QPushButton::clicked, this, &MainWindow::showAboutDialog);
    connect(m_fit10MbButton, &QPushButton::clicked, this, [this]() { fitToDiscordLimit(9.3); });
    connect(m_fit20MbButton, &QPushButton::clicked, this, [this]() { fitToDiscordLimit(18.8); });

    ffmpegProcess->setProcessChannelMode(QProcess::SeparateChannels);
    connect(ffmpegProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::updateProgress);
    connect(ffmpegProcess, &QProcess::readyReadStandardError,
            this, &MainWindow::onEncodingErrorOutput);
    connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onEncodingFinished);
    connect(ffmpegProcess, &QProcess::errorOccurred,
            this, &MainWindow::onEncodingProcessError);

    connect(m_proxyProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProxyFinished);

    if (!initializeBinaryPaths()) {
        qDebug() << "FFmpeg initialization failed";
        ui->inputButton->setEnabled(false);
        ui->outputButton->setEnabled(false);
        ui->startButton->setEnabled(false);
    } else {
        detectAvailableEncoders();
    }

    connect(ui->videoBitrateSlider, &QSlider::valueChanged,
            this, [this](int value) {

                m_userAdjustedVideoBitrate = true;

                if (m_sourceInfo.videoBitrate > 0) {
                    int percent = int((qint64(value) * 100) / m_sourceInfo.videoBitrate);
                    ui->videoBitrateLabel->setText(
                        QString("Bitrate: %1 kbps (%2%)")
                            .arg(value)
                            .arg(percent));
                } else {
                    ui->videoBitrateLabel->setText(
                        QString("Bitrate: %1 kbps").arg(value));
                }

                updateEstimatedFileSize();
            });

    ui->videoBitrateSlider->setSingleStep(100);
    ui->videoBitrateSlider->setPageStep(500);
    ui->videoBitrateSlider->setEnabled(false);

    connect(ui->fpsSlider, &QSlider::valueChanged,
            this, [this](int value) {

                ui->fpsLabel->setText(
                    QString("FPS: %1").arg(value));

                updateEstimatedFileSize();
            });

    ui->fpsSlider->setEnabled(false);

    connect(ui->audioBitrateSlider, &QSlider::valueChanged,
            this, [this](int value) {

                if (m_sourceInfo.audioBitrate > 0) {
                    int percent =
                        int((qint64(value) * 100) / m_sourceInfo.audioBitrate);

                    ui->audioBitrateLabel->setText(
                        QString("Audio bitrate: %1 kbps (%2%)")
                            .arg(value)
                            .arg(percent));
                } else {
                    ui->audioBitrateLabel->setText(
                        QString("Audio bitrate: %1 kbps").arg(value));
                }

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

    connect(m_encoderCombo, &QComboBox::currentIndexChanged,
            this, [this](int) {
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
    if (m_proxyProcess && m_proxyProcess->state() != QProcess::NotRunning) {
        m_proxyProcess->kill();
        m_proxyProcess->waitForFinished(1000);
    }
    if (ffmpegProcess && ffmpegProcess->state() != QProcess::NotRunning) {
        ffmpegProcess->kill();
        ffmpegProcess->waitForFinished(1000);
    }
    if (m_player) {
        m_player->clearSource();
    }
    cleanupPreviewProxy();
    delete ui;
}

void MainWindow::applyModernTheme()
{
    // Discord-inspired dark theme
    setStyleSheet(R"(
        QMainWindow, QWidget#centralwidget {
            background-color: #1E1F22;
            color: #F2F3F5;
            font-family: "Segoe UI", "Inter", sans-serif;
            font-size: 13px;
        }
        QFrame#videoDetails, QFrame#frame_2 {
            background-color: #2B2D31;
            border: 1px solid #383A40;
            border-radius: 10px;
        }
        QWidget#videoContainer {
            background-color: #2B2D31;
            border: 1px solid #383A40;
            border-radius: 10px;
            padding: 8px;
        }
        QLabel {
            color: #DBDEE1;
            background: transparent;
        }
        QLabel#codecLabel {
            color: #949BA4;
            font-weight: 600;
            font-size: 12px;
        }
        QLabel#durationLabel {
            color: #F2F3F5;
            font-weight: 600;
        }
        QLabel#fileSizeLabel {
            font-weight: 700;
            font-size: 13px;
            padding: 4px 8px;
            border-radius: 6px;
            background-color: #1E1F22;
        }
        QLabel#timeBadge {
            color: #B5BAC1;
            font-family: "Consolas", "Cascadia Mono", monospace;
            font-size: 12px;
            padding: 2px 6px;
            background-color: #1E1F22;
            border-radius: 4px;
        }
        QLabel#timeCurrent {
            color: #F2F3F5;
            font-family: "Consolas", "Cascadia Mono", monospace;
            font-size: 12px;
            font-weight: 600;
        }
        QPlainTextEdit {
            background-color: #1E1F22;
            color: #DBDEE1;
            border: 1px solid #3F4147;
            border-radius: 6px;
            padding: 4px 8px;
        }
        QComboBox {
            background-color: #1E1F22;
            color: #F2F3F5;
            border: 1px solid #3F4147;
            border-radius: 6px;
            padding: 5px 10px;
            min-height: 20px;
        }
        QComboBox:hover {
            border-color: #5865F2;
        }
        QComboBox::drop-down {
            border: none;
            width: 22px;
        }
        QComboBox QAbstractItemView {
            background-color: #2B2D31;
            color: #F2F3F5;
            selection-background-color: #5865F2;
            border: 1px solid #3F4147;
        }
        QPushButton {
            background-color: #383A40;
            color: #F2F3F5;
            border: 1px solid #4E5058;
            border-radius: 6px;
            padding: 5px 10px;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #474A52;
            border-color: #6D707A;
        }
        QPushButton:pressed {
            background-color: #313338;
        }
        QPushButton:disabled {
            background-color: #26272B;
            color: #5C5F66;
            border-color: #313338;
        }
        QPushButton#startButton, QPushButton#playPauseButton {
            background-color: #5865F2;
            color: #FFFFFF;
            border: none;
        }
        QPushButton#startButton:hover, QPushButton#playPauseButton:hover {
            background-color: #4752C4;
        }
        QPushButton#startButton:pressed, QPushButton#playPauseButton:pressed {
            background-color: #3C45A5;
        }
        QPushButton#startButton:disabled, QPushButton#playPauseButton:disabled {
            background-color: #3A3F73;
            color: #949BA4;
        }
        QPushButton#fitButton {
            background-color: #248046;
            color: #FFFFFF;
            border: none;
            padding: 6px 8px;
        }
        QPushButton#fitButton:hover {
            background-color: #1A6334;
        }
        QPushButton#fitButton:disabled {
            background-color: #26272B;
            color: #5C5F66;
        }
        QPushButton#presetButton {
            background-color: #313338;
            color: #DBDEE1;
            border: 1px solid #4E5058;
            padding: 4px 9px;
            font-size: 12px;
        }
        QPushButton#presetButton:hover {
            background-color: #5865F2;
            color: #FFFFFF;
            border-color: #5865F2;
        }
        QPushButton#aboutButton {
            background: transparent;
            border: none;
            color: #B5BAC1;
            font-size: 16px;
        }
        QPushButton#aboutButton:hover {
            color: #FFFFFF;
            background-color: #383A40;
        }
        QSlider::groove:horizontal {
            height: 6px;
            background: #1E1F22;
            border-radius: 3px;
        }
        QSlider::sub-page:horizontal {
            background: #5865F2;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #F2F3F5;
            width: 14px;
            height: 14px;
            margin: -4px 0;
            border-radius: 7px;
        }
        QSlider::handle:horizontal:hover {
            background: #FFFFFF;
        }
        QSlider::handle:horizontal:disabled {
            background: #4E5058;
        }
        QProgressBar {
            background-color: #1E1F22;
            border: 1px solid #3F4147;
            border-radius: 6px;
            text-align: center;
            color: #F2F3F5;
            font-weight: 600;
        }
        QProgressBar::chunk {
            background-color: #5865F2;
            border-radius: 5px;
        }
        QStatusBar, QMenuBar {
            background-color: #1E1F22;
            color: #949BA4;
        }
        QToolTip {
            background-color: #111214;
            color: #F2F3F5;
            border: 1px solid #3F4147;
            padding: 4px 8px;
            border-radius: 4px;
        }
    )");
}

void MainWindow::setupShortcuts()
{
    auto addShortcut = [this](const QKeySequence &key, auto slot) {
        auto *sc = new QShortcut(key, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, m_player, slot);
    };

    addShortcut(QKeySequence(Qt::Key_Space), &Player::togglePlayPause);
    addShortcut(QKeySequence(Qt::Key_Left),  &Player::stepFrameBackward);
    addShortcut(QKeySequence(Qt::Key_Right), &Player::stepFrameForward);
    addShortcut(QKeySequence(Qt::Key_I),     &Player::markStartAtCurrentFrame);
    addShortcut(QKeySequence(Qt::Key_O),     &Player::markEndAtCurrentFrame);
    addShortcut(QKeySequence(Qt::Key_Home),  &Player::goToStart);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            const QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
            if (ext == "mp4" || ext == "mkv" || ext == "webm" || ext == "avi" || ext == "mov") {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;

    for (const QUrl &url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == "mp4" || ext == "mkv" || ext == "webm" || ext == "avi" || ext == "mov") {
            loadInputFile(path);
            event->acceptProposedAction();
            return;
        }
    }
}

bool MainWindow::testVideoEncoder(const QString &encoderName)
{
    QProcess proc;
    proc.setProgram(ffmpegPath);
    proc.setArguments({
        "-hide_banner",
        "-loglevel", "error",
        "-f", "lavfi",
        "-i", "color=black:s=256x256:d=0.04",
        "-frames:v", "1",
        "-pix_fmt", "yuv420p",
        "-c:v", encoderName,
        "-f", "null", "-"
    });
    proc.start();
    return proc.waitForFinished(4000) && proc.exitCode() == 0;
}

void MainWindow::detectAvailableEncoders()
{
    m_encoderCombo->clear();

    QProcess proc;
    proc.setProgram(ffmpegPath);
    proc.setArguments({"-hide_banner", "-encoders"});
    proc.start();

    QString encoderList;
    if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
        encoderList = QString::fromUtf8(proc.readAllStandardOutput());
    }

    if (encoderList.contains("h264_nvenc")) {
        m_hasH264Nvenc = testVideoEncoder("h264_nvenc");
    }
    if (encoderList.contains("av1_nvenc")) {
        m_hasAv1Nvenc = testVideoEncoder("av1_nvenc");
    }

    qDebug() << "Detected hardware encoders - h264_nvenc:" << m_hasH264Nvenc
             << "av1_nvenc:" << m_hasAv1Nvenc;

    if (m_hasH264Nvenc) {
        m_encoderCombo->addItem("H.264 (NVIDIA NVENC)", "h264_nvenc");
    }
    m_encoderCombo->addItem("H.264 (CPU - libx264)", "libx264");
    if (m_hasAv1Nvenc) {
        m_encoderCombo->addItem("AV1 (NVIDIA NVENC)", "av1_nvenc");
    }

    m_encoderCombo->setCurrentIndex(0);
    m_encoderCombo->setEnabled(true);
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
    if (!process.waitForFinished(10000))
        return info;

    const QByteArray output = process.readAllStandardOutput();
    const QJsonDocument doc = QJsonDocument::fromJson(output);

    if (!doc.isObject())
        return info;

    const QJsonObject root = doc.object();

    // ---------- FORMAT (container) ----------
    const QJsonObject format = root["format"].toObject();

    info.duration = format["duration"].toString().toDouble();
    info.bitrate = parseBitrateKbps(format);

    // ---------- STREAMS ----------
    const QJsonArray streams = root["streams"].toArray();

    for (const QJsonValue &v : streams) {
        const QJsonObject s = v.toObject();
        const QString type = s["codec_type"].toString();

        if (type == "video" && !info.hasVideo) {
            // Skip embedded thumbnails / cover art
            const QJsonObject disp = s["disposition"].toObject();
            if (disp["attached_pic"].toInt() == 1)
                continue;

            info.hasVideo = true;
            info.videoCodec = s["codec_name"].toString();
            info.pixFmt = s["pix_fmt"].toString();
            info.width  = s["width"].toInt();
            info.height = s["height"].toInt();

            // Prefer avg_frame_rate over r_frame_rate: NVIDIA ShadowPlay VFR recordings
            // can report bogus r_frame_rate values like 90000/1 or 1000/1.
            double fps = parseRationalFps(s["avg_frame_rate"].toString());
            if (fps <= 0.5 || fps > 240.0) {
                fps = parseRationalFps(s["r_frame_rate"].toString());
            }
            if (fps <= 0.5 || fps > 240.0) {
                fps = 30.0;
            }
            info.fps = fps;

            info.videoBitrate = parseBitrateKbps(s);

            if (info.duration <= 0.0 && s.contains("duration")) {
                info.duration = s["duration"].toString().toDouble();
            }
        }
        else if (type == "audio" && !info.hasAudio) {
            info.hasAudio = true;
            info.audioCodec = s["codec_name"].toString();
            info.audioBitrate = parseBitrateKbps(s);
        }
    }

    // Fallback bitrate estimation for AV1 / MKV / VFR streams that omit stream bit_rate
    if (info.hasAudio && info.audioBitrate <= 0) {
        info.audioBitrate = 128;
    }

    if (info.hasVideo && info.videoBitrate <= 0) {
        qint64 containerKbps = info.bitrate;
        if (containerKbps <= 0 && info.duration > 0.0) {
            const qint64 fileSizeBytes = QFileInfo(filePath).size();
            if (fileSizeBytes > 0) {
                containerKbps = qint64((double(fileSizeBytes) * 8.0) / (info.duration * 1000.0));
            }
        }
        const qint64 audioSub = info.hasAudio ? info.audioBitrate : 0;
        info.videoBitrate = qMax<qint64>(500, containerKbps - audioSub);
    }

    return info;
}

bool MainWindow::initializeBinaryPaths()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QDir binariesDir(appDir + "/binaries");

    QString ffmpegCandidate = binariesDir.absoluteFilePath("ffmpeg");
    QString ffprobeCandidate = binariesDir.absoluteFilePath("ffprobe");

#ifdef Q_OS_WIN
    ffmpegCandidate += ".exe";
    ffprobeCandidate += ".exe";
#endif

    qDebug() << "Looking for FFmpeg binaries";
    qDebug() << "ffmpeg candidate:" << ffmpegCandidate;
    qDebug() << "ffprobe candidate:" << ffprobeCandidate;

    bool localBinariesExist = QFile::exists(ffmpegCandidate) && QFile::exists(ffprobeCandidate);

    if (localBinariesExist) {
        this->ffmpegPath = ffmpegCandidate;
        this->ffprobePath = ffprobeCandidate;
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

void MainWindow::cleanupPreviewProxy()
{
    if (!m_previewProxyPath.isEmpty()) {
        if (QFile::exists(m_previewProxyPath)) {
            QFile::remove(m_previewProxyPath);
        }
        m_previewProxyPath.clear();
    }
}

void MainWindow::onPlaybackFailed(const QString &reason)
{
    qWarning() << "Player playback failed:" << reason;

    if (inputFilePath.isEmpty() || !m_sourceInfo.hasVideo)
        return;

    if (m_proxyAttempted) {
        m_player->setOverlayStatus("Preview unavailable (you can still trim & compress)", false);
        return;
    }

    m_proxyAttempted = true;
    generatePreviewProxy(m_hasH264Nvenc);
}

void MainWindow::generatePreviewProxy(bool useHardware)
{
    if (m_proxyProcess->state() != QProcess::NotRunning) {
        m_proxyProcess->kill();
        m_proxyProcess->waitForFinished(1000);
    }

    m_player->clearSource();
    cleanupPreviewProxy();

    m_proxyUsedHw = useHardware;
    m_previewProxyPath = QDir::tempPath() + QString("/clip2disc_preview_%1.mp4")
                                                .arg(QCoreApplication::applicationPid());

    m_player->setOverlayStatus(
        QString("Generating compatible preview (%1)...")
            .arg(m_sourceInfo.videoCodec.toUpper()),
        false);

    QStringList args;
    args << "-y"
         << "-i" << inputFilePath
         << "-map" << "0:v:0";

    if (m_sourceInfo.hasAudio) {
        args << "-map" << "0:a:0?";
    }

    args << "-vf" << "scale='min(1280,iw)':-2"
         << "-pix_fmt" << "yuv420p";

    if (useHardware) {
        args << "-c:v" << "h264_nvenc"
             << "-preset" << "p1"
             << "-rc" << "vbr"
             << "-cq" << "28"
             << "-b:v" << "0";
    } else {
        args << "-c:v" << "libx264"
             << "-preset" << "ultrafast"
             << "-crf" << "28";
    }

    if (m_sourceInfo.hasAudio) {
        args << "-c:a" << "aac"
             << "-b:a" << "96k";
    } else {
        args << "-an";
    }

    args << "-movflags" << "+faststart"
         << "-loglevel" << "error"
         << m_previewProxyPath;

    qDebug() << "Generating preview proxy:" << ffmpegPath << args;
    m_proxyProcess->setProgram(ffmpegPath);
    m_proxyProcess->setArguments(args);
    m_proxyProcess->start();
}

void MainWindow::onProxyFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && QFile::exists(m_previewProxyPath)) {
        qDebug() << "Preview proxy generated successfully:" << m_previewProxyPath;
        m_player->setPreviewSourceFile(m_previewProxyPath);
        return;
    }

    qWarning() << "Preview proxy failed (exit:" << exitCode << "):"
               << QString::fromUtf8(m_proxyProcess->readAllStandardError());

    // If hardware proxy failed (e.g. NVENC session limit), retry once with CPU ultrafast
    if (m_proxyUsedHw) {
        generatePreviewProxy(false);
        return;
    }

    m_player->setOverlayStatus("Preview unavailable (you can still trim & compress)", false);
}

void MainWindow::updateEstimatedFileSize()
{
    if (!m_player || m_sourceInfo.duration <= 0) {
        ui->fileSizeLabel->setText("Est. size: —");
        if (m_estimateHintLabel)
            m_estimateHintLabel->setText("Select a video to estimate size");
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
                  : qMax(1, int(std::round(m_sourceInfo.fps)));

    // --- Video bitrate (scaled) ---
    int userVideoBitrate = ui->videoBitrateSlider->value();
    int videoBitrate =
        computeScaledVideoBitrate(userVideoBitrate, outW, outH, fps);

    // Show real effective video bitrate when resolution/FPS scaling lowers it
    if (videoBitrate != userVideoBitrate) {
        ui->videoBitrateLabel->setText(
            QString("Bitrate: %1 kbps (eff. %2)")
                .arg(userVideoBitrate)
                .arg(videoBitrate));
    } else if (m_sourceInfo.videoBitrate > 0) {
        int percent = int((qint64(userVideoBitrate) * 100) / m_sourceInfo.videoBitrate);
        ui->videoBitrateLabel->setText(
            QString("Bitrate: %1 kbps (%2%)")
                .arg(userVideoBitrate)
                .arg(percent));
    }

    // --- Audio bitrate ---
    int audioBitrate = 0;
    if (m_sourceInfo.hasAudio) {
        audioBitrate = ui->audioBitrateSlider->isEnabled()
                           ? ui->audioBitrateSlider->value()
                           : int(m_sourceInfo.audioBitrate);
    }

    double totalBitrateKbps = videoBitrate + audioBitrate;
    if (totalBitrateKbps <= 0) {
        ui->fileSizeLabel->setText("Est. size: —");
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

    const QString encoder = m_encoderCombo ? m_encoderCombo->currentData().toString() : QStringLiteral("libx264");
    const double minHealthyBpp = (encoder == "av1_nvenc") ? 0.025
                                 : (encoder == "libx264") ? 0.045
                                                          : 0.050;
    const double pixelsPerSec = double(outW) * outH * qMax(1, fps);
    const double bpp = (pixelsPerSec > 0.0) ? ((videoBitrate * 1000.0) / pixelsPerSec) : 0.0;
    const bool lowMotionQuality = (videoBitrate > 0 && bpp > 0.0 && bpp < minHealthyBpp);

    QString badge;
    QString colorHex;
    QString hint;
    QString hintColor = "#949BA4";

    if (sizeMB <= 10.0) {
        badge = "✓ <10 MB";
        colorHex = "#23A55A";
        hint = QString("Fits Discord Mobile & PC (%1 kbps • %2s)")
                   .arg(int(totalBitrateKbps))
                   .arg(durationSec, 0, 'f', 1);
    } else if (sizeMB <= 20.0) {
        badge = "✓ <20 MB";
        colorHex = "#7983F5";
        hint = QString("Fits Discord PC (%1 kbps • %2s)")
                   .arg(int(totalBitrateKbps))
                   .arg(durationSec, 0, 'f', 1);
    } else {
        badge = "⚠ >20 MB";
        colorHex = "#F23F43";
        hint = QString("Too large (%1 kbps • %2s) — lower bitrate/res, trim, or click Auto-fit")
                   .arg(int(totalBitrateKbps))
                   .arg(durationSec, 0, 'f', 1);
    }

    if (lowMotionQuality && sizeMB <= 20.0) {
        hint += QString("\n⚠ Low bitrate for %1p@%2fps — motion may pixelate (lower res/FPS or use AV1)")
                    .arg(outH)
                    .arg(fps);
        hintColor = "#F0B232";
    }

    ui->fileSizeLabel->setText(
        QString("Est. size: ~%1 MB  (%2)")
            .arg(sizeMB, 0, 'f', 1)
            .arg(badge));

    ui->fileSizeLabel->setStyleSheet(
        QString("color: %1; font-weight: 700; padding: 5px 8px; border-radius: 6px; background-color: #1E1F22;")
            .arg(colorHex));

    if (m_estimateHintLabel) {
        m_estimateHintLabel->setText(hint);
        m_estimateHintLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(hintColor));
    }
}

void MainWindow::fitToDiscordLimit(double targetSizeMB)
{
    if (!m_player || m_sourceInfo.duration <= 0)
        return;

    double durationSec = m_sourceInfo.duration;
    const qint64 startMs = m_player->trimStart();
    const qint64 endMs   = m_player->trimEnd();
    if (endMs > startMs)
        durationSec = (endMs - startMs) / 1000.0;

    if (durationSec <= 0.1)
        return;

    const double totalTargetKbps = (targetSizeMB * 8.0 * 1024.0) / durationSec;

    int audioBitrate = 0;
    if (m_sourceInfo.hasAudio) {
        audioBitrate = ui->audioBitrateSlider->value();
        // Trim audio bitrate slightly so video gets maximum motion quality
        if (audioBitrate > 128 && totalTargetKbps < 4500.0) {
            audioBitrate = qBound(ui->audioBitrateSlider->minimum(), 128, ui->audioBitrateSlider->maximum());
            ui->audioBitrateSlider->setValue(audioBitrate);
        }
        if (audioBitrate > 96 && totalTargetKbps < 1500.0) {
            audioBitrate = qBound(ui->audioBitrateSlider->minimum(), 96, ui->audioBitrateSlider->maximum());
            ui->audioBitrateSlider->setValue(audioBitrate);
        }
        if (audioBitrate > 64 && totalTargetKbps < 800.0) {
            audioBitrate = qBound(ui->audioBitrateSlider->minimum(), 64, ui->audioBitrateSlider->maximum());
            ui->audioBitrateSlider->setValue(audioBitrate);
        }
    }

    const double targetEffectiveVideoKbps = qMax(250.0, totalTargetKbps - audioBitrate);

    // Choose the highest resolution + FPS that preserves clean motion quality (bits per pixel)
    const QString encoder = m_encoderCombo ? m_encoderCombo->currentData().toString() : QStringLiteral("libx264");
    const double minTargetBpp = (encoder == "av1_nvenc") ? 0.028
                                : (encoder == "libx264") ? 0.048
                                                         : 0.055;

    const int srcFps = qBound(1, int(std::round(m_sourceInfo.fps)), 240);
    int bestResIndex = ui->resolutionCombo->count() - 1;
    int bestW = m_sourceInfo.width;
    int bestH = m_sourceInfo.height;
    int bestFps = qMin(srcFps, 30);
    bool found = false;

    auto parseComboRes = [&](int idx, int &w, int &h) -> bool {
        const QStringList p = ui->resolutionCombo->itemData(idx).toString().split("x");
        if (p.size() != 2) return false;
        w = p[0].toInt();
        h = p[1].toInt();
        return w > 0 && h > 0;
    };

    // Pass 1: keep source FPS and pick highest resolution >= 720p with healthy bpp
    for (int i = 0; i < ui->resolutionCombo->count(); ++i) {
        int w = 0, h = 0;
        if (!parseComboRes(i, w, h) || h < 720)
            continue;
        const double bpp = (targetEffectiveVideoKbps * 1000.0) / (double(w) * h * srcFps);
        if (bpp >= minTargetBpp) {
            bestResIndex = i;
            bestW = w;
            bestH = h;
            bestFps = srcFps;
            found = true;
            break;
        }
    }

    // Pass 2: if 720p @ source FPS is still tight, prefer lowering FPS (48 -> 30) at 720p+
    // before dropping resolution below 720p
    if (!found) {
        for (int candidateFps : {48, 30}) {
            if (candidateFps >= srcFps)
                continue;
            for (int i = 0; i < ui->resolutionCombo->count(); ++i) {
                int w = 0, h = 0;
                if (!parseComboRes(i, w, h) || h < 720)
                    continue;
                const double bpp = (targetEffectiveVideoKbps * 1000.0) / (double(w) * h * candidateFps);
                if (bpp >= minTargetBpp) {
                    bestResIndex = i;
                    bestW = w;
                    bestH = h;
                    bestFps = candidateFps;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }

    // Pass 3: very long clips — step down below 720p at 30fps
    if (!found) {
        const int lowFps = qMin(srcFps, 30);
        for (int i = 0; i < ui->resolutionCombo->count(); ++i) {
            int w = 0, h = 0;
            if (!parseComboRes(i, w, h))
                continue;
            bestResIndex = i;
            bestW = w;
            bestH = h;
            bestFps = lowFps;
            const double bpp = (targetEffectiveVideoKbps * 1000.0) / (double(w) * h * lowFps);
            if (bpp >= minTargetBpp)
                break;
        }
    }

    if (bestResIndex >= 0 && bestResIndex < ui->resolutionCombo->count()) {
        ui->resolutionCombo->blockSignals(true);
        ui->resolutionCombo->setCurrentIndex(bestResIndex);
        ui->resolutionCombo->blockSignals(false);
    }

    if (ui->fpsSlider->isEnabled()) {
        const int clampedFps = qBound(ui->fpsSlider->minimum(), bestFps, ui->fpsSlider->maximum());
        ui->fpsSlider->blockSignals(true);
        ui->fpsSlider->setValue(clampedFps);
        ui->fpsSlider->blockSignals(false);
        ui->fpsLabel->setText(QString("FPS: %1").arg(clampedFps));
        bestFps = clampedFps;
    }

    // Invert computeScaledVideoBitrate's factor so estimated & actual output hit targetSizeMB
    const double refPixels = 1920.0 * 1080.0;
    const double curPixels = double(bestW) * bestH;
    const double resFactor = qBound(0.15, curPixels / refPixels, 1.0);
    const double fpsFactor = qBound(0.35, bestFps / 60.0, 1.0);

    int neededSliderValue = int(targetEffectiveVideoKbps / (resFactor * fpsFactor));
    if (neededSliderValue > ui->videoBitrateSlider->maximum() &&
        targetEffectiveVideoKbps <= m_sourceInfo.videoBitrate) {
        ui->videoBitrateSlider->setMaximum(neededSliderValue);
    }
    neededSliderValue = qBound(ui->videoBitrateSlider->minimum(),
                               neededSliderValue,
                               ui->videoBitrateSlider->maximum());

    m_userAdjustedVideoBitrate = true;
    ui->videoBitrateSlider->setValue(neededSliderValue);
    updateEstimatedFileSize();
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
        qBound(ui->videoBitrateSlider->minimum(),
               int(m_sourceInfo.videoBitrate * scale),
               ui->videoBitrateSlider->maximum());

    ui->videoBitrateSlider->blockSignals(true);
    ui->videoBitrateSlider->setValue(newBitrate);
    ui->videoBitrateSlider->blockSignals(false);

    ui->videoBitrateLabel->setText(
        QString("Bitrate: %1 kbps (auto)")
            .arg(newBitrate));
}

void MainWindow::selectInputFile()
{
    QString selected = QFileDialog::getOpenFileName(
        this,
        "Select Video File",
        "",
        "Videos (*.mp4 *.mkv *.webm *.avi *.mov)"
        );

    if (selected.isEmpty())
        return;

    loadInputFile(selected);
}

void MainWindow::loadInputFile(const QString &selected)
{
    if (selected.isEmpty() || !QFile::exists(selected))
        return;

    m_userAdjustedVideoBitrate = false;

    // Cancel any previous preview proxy generation
    if (m_proxyProcess->state() != QProcess::NotRunning) {
        m_proxyProcess->kill();
        m_proxyProcess->waitForFinished(1000);
    }
    m_player->clearSource();
    cleanupPreviewProxy();
    m_proxyAttempted = false;

    inputFilePath = selected;
    ui->inputLabel->setPlainText(inputFilePath);

    m_sourceInfo = probeVideo(inputFilePath);

    if (!m_sourceInfo.hasVideo || m_sourceInfo.width <= 0 || m_sourceInfo.height <= 0 || m_sourceInfo.duration <= 0.0) {
        QMessageBox::warning(this, "Invalid Video",
                             "Could not read video metadata with ffprobe.\n"
                             "Please verify that the selected file is a valid video.");
        return;
    }

    // ---- Static info ----
    ui->durationLabel->setText(
        QString::number(m_sourceInfo.duration, 'f', 1) + " s");

    const QString audioLabel = m_sourceInfo.hasAudio ? m_sourceInfo.audioCodec : QStringLiteral("none");
    ui->codecLabel->setText(
        QString("V: %1 | A: %2")
            .arg(m_sourceInfo.videoCodec)
            .arg(audioLabel));

    // ---- Video bitrate ----
    const int maxVideoBitrate =
        qMax<int>(200, int(m_sourceInfo.videoBitrate));
    const int minVideoBitrate = qMin(200, maxVideoBitrate);

    ui->videoBitrateSlider->blockSignals(true);
    ui->videoBitrateSlider->setEnabled(true);
    ui->videoBitrateSlider->setMinimum(minVideoBitrate);
    ui->videoBitrateSlider->setMaximum(maxVideoBitrate);
    ui->videoBitrateSlider->setValue(maxVideoBitrate);
    ui->videoBitrateSlider->blockSignals(false);

    ui->videoBitrateLabel->setText(
        QString("Bitrate: %1 kbps").arg(maxVideoBitrate));

    // ---- FPS ----
    const int maxFps = qBound(1, int(std::round(m_sourceInfo.fps)), 240);

    ui->fpsSlider->blockSignals(true);
    ui->fpsSlider->setEnabled(true);
    ui->fpsSlider->setMinimum(1);
    ui->fpsSlider->setMaximum(maxFps);
    ui->fpsSlider->setValue(maxFps);
    ui->fpsSlider->blockSignals(false);

    ui->fpsLabel->setText(
        QString("FPS: %1").arg(maxFps));

    // ---- Audio bitrate ----
    if (m_sourceInfo.hasAudio) {
        const int maxAudioBitrate =
            qMax<int>(32, int(m_sourceInfo.audioBitrate));

        ui->audioBitrateSlider->blockSignals(true);
        ui->audioBitrateSlider->setEnabled(true);
        ui->audioBitrateSlider->setMinimum(32);
        ui->audioBitrateSlider->setMaximum(maxAudioBitrate);
        ui->audioBitrateSlider->setValue(maxAudioBitrate);
        ui->audioBitrateSlider->blockSignals(false);

        ui->audioBitrateLabel->setText(
            QString("Audio bitrate: %1 kbps").arg(maxAudioBitrate));
    } else {
        ui->audioBitrateSlider->blockSignals(true);
        ui->audioBitrateSlider->setEnabled(false);
        ui->audioBitrateSlider->blockSignals(false);
        ui->audioBitrateLabel->setText("Audio: none");
    }

    // ---- Resolution ----
    ui->resolutionCombo->blockSignals(true);
    ui->resolutionCombo->clear();

    int w = m_sourceInfo.width;
    int h = m_sourceInfo.height;

    auto addIfMissing = [&](const QString &resValue, const QString &resLabel) {
        for (int i = 0; i < ui->resolutionCombo->count(); ++i) {
            if (ui->resolutionCombo->itemData(i).toString() == resValue)
                return;
        }
        ui->resolutionCombo->addItem(resLabel, resValue);
    };

    QString originalRes = QString("%1x%2").arg(w).arg(h);
    addIfMissing(originalRes, originalRes + " (Original)");

    if (w >= 2560 && h >= 1440)
        addIfMissing("2560x1440", "2560x1440");

    if (w >= 1920 && h >= 1080)
        addIfMissing("1920x1080", "1920x1080");

    if (w >= 1280 && h >= 720)
        addIfMissing("1280x720", "1280x720");

    if (w >= 854 && h >= 480)
        addIfMissing("854x480", "854x480");

    if (w >= 640 && h >= 360)
        addIfMissing("640x360", "640x360");

    ui->resolutionCombo->setCurrentIndex(0);
    ui->resolutionCombo->setEnabled(true);
    ui->resolutionCombo->blockSignals(false);

    m_fit10MbButton->setEnabled(true);
    m_fit20MbButton->setEnabled(true);
    ui->progressBar->setValue(0);

    // ---- Output path ----
    QFileInfo inputInfo(inputFilePath);
    outputFilePath =
        inputInfo.absolutePath() + "/" +
        inputInfo.completeBaseName() +
        "-clipped.mp4";

    ui->outputLabel->setPlainText(outputFilePath);

    // ---- Player ----
    // Initialize timeline duration from ffprobe first so trimming works even if preview falls back
    m_player->setFrameRate(m_sourceInfo.fps);
    m_player->setExternalDuration(qint64(m_sourceInfo.duration * 1000.0));
    m_player->setSourceFile(inputFilePath, m_sourceInfo.hasVideo);

    updateMarkedDuration(0, qint64(m_sourceInfo.duration * 1000.0));
    updateEstimatedFileSize();
}

void MainWindow::selectOutputFile()
{
    QString chosen = QFileDialog::getSaveFileName(
        this, "Select Output File", outputFilePath, "MP4 Files (*.mp4)");
    if (!chosen.isEmpty()) {
        if (!chosen.endsWith(".mp4", Qt::CaseInsensitive))
            chosen += ".mp4";
        outputFilePath = chosen;
        ui->outputLabel->setPlainText(outputFilePath);
    }
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

void MainWindow::setEncodingUiBusy(bool busy)
{
    ui->inputButton->setEnabled(!busy);
    ui->outputButton->setEnabled(!busy);
    ui->startButton->setEnabled(!busy);
    ui->resolutionCombo->setEnabled(!busy);
    ui->fpsSlider->setEnabled(!busy);
    ui->videoBitrateSlider->setEnabled(!busy);
    ui->audioBitrateSlider->setEnabled(!busy && m_sourceInfo.hasAudio);
    m_encoderCombo->setEnabled(!busy);
    m_fit10MbButton->setEnabled(!busy);
    m_fit20MbButton->setEnabled(!busy);
}

void MainWindow::startEncoding()
{
    qDebug() << "Start encoding clicked";

    if (ffmpegProcess->state() != QProcess::NotRunning)
        return;

    if (inputFilePath.isEmpty() || outputFilePath.isEmpty()) {
        QMessageBox::warning(this, "Warning",
                             "Please select both input and output files.");
        return;
    }

    if (QFileInfo(inputFilePath).canonicalFilePath() == QFileInfo(outputFilePath).canonicalFilePath()) {
        QMessageBox::warning(this, "Warning",
                             "Output file cannot overwrite the input file. Please choose a different output path.");
        return;
    }

    m_player->pause();

    // --- Trim ---
    qint64 trimStartMs = m_player->trimStart();
    qint64 trimEndMs   = m_player->trimEnd();
    qint64 fullDurMs   = qint64(m_sourceInfo.duration * 1000.0);

    bool hasTrim =
        trimEndMs > trimStartMs &&
        (trimStartMs > 0 || (fullDurMs > 0 && trimEndMs < fullDurMs - 50));

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
    videoBitrate = qMax(100, videoBitrate);
    QString videoBitrateArg = QString::number(videoBitrate) + "k";
    QString fpsArg = QString::number(fps);
    QString scaleFilter = QString("scale=%1:%2:flags=lanczos")
                              .arg(qMax(2, (outW / 2) * 2))
                              .arg(qMax(2, (outH / 2) * 2));

    QString encoder = m_encoderCombo->currentData().toString();
    if (encoder.isEmpty())
        encoder = "libx264";

    QStringList args;
    args << "-y";

    if (hasTrim) {
        args << "-ss" << QString::number(trimStartSec, 'f', 3);
        totalDuration = trimDurationSec;
    } else {
        totalDuration = m_sourceInfo.duration;
    }

    args << "-i" << inputFilePath;

    if (hasTrim) {
        args << "-t" << QString::number(trimDurationSec, 'f', 3);
    }

    // Map primary video and optional primary audio (ignores ShadowPlay data tracks / thumbnails)
    args << "-map" << "0:v:0";
    if (m_sourceInfo.hasAudio) {
        args << "-map" << "0:a:0?";
    }

    // --- Video ---
    // Always enforce yuv420p so 10-bit AV1 / HDR sources remain playable on Discord.
    // Allow 1.5x maxrate headroom for high-motion bursts while preserving the average -b:v
    // size target, use a 2-second keyframe interval, and enable NVENC HQ multipass + AQ.
    const int maxrateKbps = qMax(videoBitrate + 200, int(std::round(videoBitrate * 1.5)));
    const int bufsizeKbps = videoBitrate * 2;
    const int gopSize = qBound(24, fps * 2, 240);
    const QString maxrateArg = QString::number(maxrateKbps) + "k";
    const QString bufsizeArg = QString::number(bufsizeKbps) + "k";
    const QString gopArg = QString::number(gopSize);

    if (encoder == "h264_nvenc") {
        args << "-c:v" << "h264_nvenc"
             << "-preset" << "p6"
             << "-tune" << "hq"
             << "-multipass" << "fullres"
             << "-rc" << "vbr"
             << "-rc-lookahead" << "32"
             << "-spatial-aq" << "1"
             << "-temporal-aq" << "1"
             << "-aq-strength" << "8"
             << "-g" << gopArg
             << "-pix_fmt" << "yuv420p"
             << "-b:v" << videoBitrateArg
             << "-maxrate" << maxrateArg
             << "-bufsize" << bufsizeArg;
    } else if (encoder == "av1_nvenc") {
        args << "-c:v" << "av1_nvenc"
             << "-preset" << "p6"
             << "-tune" << "hq"
             << "-multipass" << "fullres"
             << "-rc" << "vbr"
             << "-rc-lookahead" << "32"
             << "-spatial-aq" << "1"
             << "-temporal-aq" << "1"
             << "-aq-strength" << "8"
             << "-g" << gopArg
             << "-pix_fmt" << "yuv420p"
             << "-b:v" << videoBitrateArg
             << "-maxrate" << maxrateArg
             << "-bufsize" << bufsizeArg;
    } else {
        args << "-c:v" << "libx264"
             << "-preset" << "medium"
             << "-g" << gopArg
             << "-pix_fmt" << "yuv420p"
             << "-b:v" << videoBitrateArg
             << "-maxrate" << maxrateArg
             << "-bufsize" << bufsizeArg;
    }

    args << "-r" << fpsArg
         << "-vf" << scaleFilter;

    // --- Audio ---
    if (m_sourceInfo.hasAudio) {
        args << "-c:a" << "aac"
             << "-b:a" << QString::number(audioBitrate) + "k";
    } else {
        args << "-an";
    }

    // --- MP4 + progress ---
    args << "-movflags" << "+faststart"
         << "-avoid_negative_ts" << "make_zero"
         << "-progress" << "pipe:1"
         << "-nostats"
         << "-loglevel" << "error";

    args << outputFilePath;

    qDebug() << "FFmpeg:" << ffmpegPath << args;

    m_ffmpegErrorLog.clear();
    ui->progressBar->setValue(0);
    setEncodingUiBusy(true);

    ffmpegProcess->setProgram(ffmpegPath);
    ffmpegProcess->setArguments(args);
    ffmpegProcess->start();
}

void MainWindow::deleteTrimmedFile(const QString &path)
{
    if (!path.isEmpty() && QFile::exists(path)) {
        qDebug() << "Deleting trimmed file:" << path;
        QFile::remove(path);
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
    int duration = int(output.toDouble(&ok));
    return ok ? duration : 0;
}

void MainWindow::updateProgress()
{
    const QString text = QString::fromUtf8(ffmpegProcess->readAllStandardOutput());

    static const QRegularExpression re("out_time_(?:us|ms)=([0-9]+)");
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    qint64 latestUs = -1;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        latestUs = match.captured(1).toLongLong();
    }

    if (latestUs >= 0 && totalDuration > 0.0) {
        const double currentSeconds = latestUs / 1000000.0;
        const int percent = qBound(0, int((currentSeconds * 100.0) / totalDuration), 99);
        ui->progressBar->setValue(percent);
    }
}

void MainWindow::onEncodingErrorOutput()
{
    m_ffmpegErrorLog += QString::fromUtf8(ffmpegProcess->readAllStandardError());
}

void MainWindow::onEncodingFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    setEncodingUiBusy(false);

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        ui->progressBar->setValue(100);
        const double sizeMB = QFileInfo(outputFilePath).size() / (1024.0 * 1024.0);
        QMessageBox msg(this);
        msg.setIcon(QMessageBox::Information);
        msg.setWindowTitle("Finished");
        msg.setText(QString("Video compressed!\nFinal size: %1 MB").arg(sizeMB, 0, 'f', 2));
        QPushButton *openFolderBtn = msg.addButton("Open Folder", QMessageBox::ActionRole);
        msg.addButton(QMessageBox::Ok);
        msg.exec();
        if (msg.clickedButton() == openFolderBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputFilePath).absolutePath()));
        }
    } else {
        ui->progressBar->setValue(0);
        QString details = m_ffmpegErrorLog.trimmed();
        if (details.isEmpty())
            details = QString("FFmpeg exited with code %1.").arg(exitCode);
        QMessageBox::critical(this, "Encoding Error",
                              "Failed to compress video:\n\n" + details);
    }
}

void MainWindow::onEncodingProcessError(QProcess::ProcessError error)
{
    setEncodingUiBusy(false);
    ui->progressBar->setValue(0);
    QMessageBox::critical(this, "FFmpeg Process Error",
                          QString("Could not run FFmpeg (error %1): %2")
                              .arg(int(error))
                              .arg(ffmpegProcess->errorString()));
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

    // Safety clamp (ensure min <= max so qBound never asserts on low bitrates)
    const int minBitrate = qMin(200, userBitrate);
    scaled = qBound(minBitrate, scaled, userBitrate);

    return scaled;
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, "About",
                       "<p><b>Clip2Disc (AV1 Support Fork)</b></p>"
                       "<p>A wrapper for FFmpeg video compressor that reduces video size to fit Discord upload limits (10MB / 20MB).</p>"
                       "<p>Includes AV1 playback & preview fallback, 10-bit to 8-bit Discord-safe encoding, NVIDIA NVENC support, and Discord 10MB/20MB auto-fit.</p>"
                       "<p><a href='https://github.com/murdoockds/clip2disc'>Original GitHub Repository</a></p>");
}
