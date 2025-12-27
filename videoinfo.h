#ifndef VIDEOINFO_H
#define VIDEOINFO_H
#include <QString>

struct VideoInfo {
    double duration = 0;
    int width = 0;
    int height = 0;
    double fps = 0;
    QString videoCodec;
    QString audioCodec;
    qint64 bitrate = 0;
};


#endif // VIDEOINFO_H
