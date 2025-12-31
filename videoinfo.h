#ifndef VIDEOINFO_H
#define VIDEOINFO_H

#include <QString>
#include <QtGlobal>   // for qint64

struct VideoInfo {
    double duration = 0.0;

    int width = 0;
    int height = 0;
    double fps = 0.0;

    QString videoCodec;
    QString audioCodec;

    qint64 bitrate = 0;       // container bitrate (kbps)
    qint64 videoBitrate = 0;  // video stream bitrate (kbps)
    qint64 audioBitrate = 0;  // audio stream bitrate (kbps)
};

#endif // VIDEOINFO_H
