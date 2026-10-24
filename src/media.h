#pragma once

#include <QString>
#include <QStringList>

#include "core.h"

namespace sc {

bool isVideoFile(const QString &fileName);

int normRotation(double r);

/// ffmpeg applies a file's display matrix on its own, so manual rotation is
/// layered on top of whatever the file already carries.
/// Order: rotation -> flips -> even dimensions -> square pixels.
QString buildVideoFilter(int rotate, bool flipH, bool flipV);

/// "none" (stripped/silent), "copy" (container-safe codec) or "aac" (re-encode).
QString audioPlan(bool hasAudio, const QString &audioCodec, bool keepAudio,
                  const QString &container);

/// "mp4" for H.264, "mov" for ProRes/DNxHR.
QString outputExtension(const QString &format);

QStringList probeArgs(const QString &input);
Meta parseProbe(const QByteArray &json, QString *error);

struct RenderSpec {
    QString input;
    QString output;
    double in = 0;
    double duration = 0;
    int rotate = 0;
    bool flipH = false;
    bool flipV = false;
    bool hasAudio = false;
    QString audioCodec;
    bool keepAudio = false;
    QString format = QStringLiteral("h264");
    int crf = 18;
    QString preset = QStringLiteral("medium");
};

QStringList renderArgs(const RenderSpec &spec);

} // namespace sc
