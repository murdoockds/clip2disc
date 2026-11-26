#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Ensure Qt Multimedia uses the FFmpeg backend (which supports D3D11VA hardware AV1
    // decoding on modern GPUs, whereas Windows Media Foundation fails on many AV1 streams).
    // If hardware AV1 decoding is unavailable or produces no frames, Player + MainWindow
    // automatically fall back to a fast FFmpeg H.264 preview proxy.
    if (!qEnvironmentVariableIsSet("QT_MEDIA_BACKEND")) {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }
#endif

    QApplication a(argc, argv);
    MainWindow w;
    w.showMaximized();
    return a.exec();
}
