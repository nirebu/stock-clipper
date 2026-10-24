#include "media.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QtMath>

namespace sc {

namespace {

/// ffprobe reports `duration` and `size` as JSON strings, not numbers.
double f64Loose(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().trimmed().toDouble(&ok);
        return ok ? d : qQNaN();
    }
    return qQNaN();
}

double parseRate(const QJsonValue &v)
{
    const QString text = v.toString();
    if (text.isEmpty())
        return 0.0;
    const QStringList parts = text.split(QLatin1Char('/'));
    bool ok = false;
    double a = parts.value(0).toDouble(&ok);
    if (!ok)
        return 0.0;
    if (parts.size() > 1) {
        bool ok2 = false;
        const double b = parts.value(1).toDouble(&ok2);
        if (ok2 && b != 0.0)
            a /= b;
    }
    if (!std::isfinite(a) || a <= 0)
        return 0.0;
    return round3(a);
}

} // namespace

bool isVideoFile(const QString &fileName)
{
    static const QStringList exts{"mp4", "m4v", "mov", "mkv", "avi", "mts", "m2ts", "m2t",
                                  "webm", "mxf", "mpeg", "mpg", "wmv", "3gp", "insv", "lrv", "ts"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return !suffix.isEmpty() && exts.contains(suffix);
}

int normRotation(double r)
{
    const int n = int(qRound(r / 90.0)) * 90;
    return ((n % 360) + 360) % 360;
}

QString buildVideoFilter(int rotate, bool flipH, bool flipV)
{
    QStringList f;
    switch (normRotation(rotate)) {
    case 90:
        f << QStringLiteral("transpose=1");
        break;
    case 180:
        f << QStringLiteral("hflip") << QStringLiteral("vflip");
        break;
    case 270:
        f << QStringLiteral("transpose=2");
        break;
    default:
        break;
    }
    if (flipH)
        f << QStringLiteral("hflip");
    if (flipV)
        f << QStringLiteral("vflip");
    // h264 + yuv420p needs even dimensions
    f << QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2");
    f << QStringLiteral("setsar=1");
    return f.join(QLatin1Char(','));
}

QString audioPlan(bool hasAudio, const QString &audioCodec, bool keepAudio,
                  const QString &container)
{
    if (!hasAudio || !keepAudio)
        return QStringLiteral("none");
    static const QStringList safe{"aac", "mp3", "ac3", "eac3", "alac"};
    const QString codec = audioCodec.toLower();
    if (safe.contains(codec))
        return QStringLiteral("copy");
    // MOV carries PCM fine; MP4 does not (and MXF is not offered).
    if (container == QLatin1String("mov") && codec.startsWith(QLatin1String("pcm_")))
        return QStringLiteral("copy");
    return QStringLiteral("aac");
}

QString outputExtension(const QString &format)
{
    return format == QLatin1String("prores") || format == QLatin1String("dnxhr")
        ? QStringLiteral("mov")
        : QStringLiteral("mp4");
}

QStringList probeArgs(const QString &input)
{
    return {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"),
            QStringLiteral("json"), QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
            input};
}

Meta parseProbe(const QByteArray &json, QString *error)
{
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("ffprobe did not return JSON");
        return {};
    }
    const QJsonObject info = doc.object();
    const QJsonArray streams = info.value("streams").toArray();

    QJsonObject video;
    QJsonObject audio;
    for (const QJsonValue &s : streams) {
        const QJsonObject obj = s.toObject();
        const QString type = obj.value("codec_type").toString();
        if (type == QLatin1String("video") && video.isEmpty()) {
            if (obj.value("disposition").toObject().value("attached_pic").toInt() == 1)
                continue;
            video = obj;
        } else if (type == QLatin1String("audio") && audio.isEmpty()) {
            audio = obj;
        }
    }
    if (video.isEmpty()) {
        if (error)
            *error = QStringLiteral("No video stream found");
        return {};
    }

    const QJsonObject format = info.value("format").toObject();
    double duration = f64Loose(format.value("duration"));
    if (!std::isfinite(duration))
        duration = f64Loose(video.value("duration"));

    double rotationSrc = 0.0;
    const QJsonArray sideData = video.value("side_data_list").toArray();
    for (const QJsonValue &d : sideData) {
        const QJsonValue r = d.toObject().value("rotation");
        if (r.isDouble()) {
            rotationSrc = r.toDouble();
            break;
        }
    }
    if (rotationSrc == 0.0) {
        const QJsonObject tags = video.value("tags").toObject();
        const QJsonValue r = tags.contains("rotate") ? tags.value("rotate") : tags.value("Rotation");
        if (r.isString()) {
            const double parsed = f64Loose(r);
            if (std::isfinite(parsed))
                rotationSrc = parsed;
        }
    }
    const int rotation = normRotation(rotationSrc);

    Meta m;
    m.duration = round3(std::isfinite(duration) ? duration : 0.0);
    m.width = video.value("width").toInt();
    m.height = video.value("height").toInt();
    const bool swapped = rotation == 90 || rotation == 270;
    m.displayWidth = swapped ? m.height : m.width;
    m.displayHeight = swapped ? m.width : m.height;
    m.rotation = rotation;
    m.fps = parseRate(video.value("avg_frame_rate"));
    if (m.fps == 0.0)
        m.fps = parseRate(video.value("r_frame_rate"));
    m.codec = video.value("codec_name").toString();
    m.pixFmt = video.value("pix_fmt").toString();
    m.hasAudio = !audio.isEmpty();
    m.audioCodec = audio.value("codec_name").toString();
    m.sizeBytes = qint64(f64Loose(format.value("size")));
    return m;
}

QStringList renderArgs(const RenderSpec &spec)
{
    const QString container = outputExtension(spec.format);
    const QString audio = audioPlan(spec.hasAudio, spec.audioCodec, spec.keepAudio, container);

    QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-y"};
    // -ss before -i: fast, but still frame-accurate while re-encoding
    if (spec.in > 0) {
        args << "-ss" << QString::number(spec.in, 'f', 3);
    }
    args << "-i" << spec.input;
    args << "-t" << QString::number(qMax(0.05, spec.duration), 'f', 3);
    args << "-map" << "0:v:0";
    if (audio != QLatin1String("none"))
        args << "-map" << "0:a:0";

    args << "-vf" << buildVideoFilter(spec.rotate, spec.flipH, spec.flipV);
    if (spec.format == QLatin1String("prores")) {
        // 422 HQ, 10-bit; the filter above still guarantees even dimensions.
        args << "-c:v" << "prores_ks" << "-profile:v" << "3"
             << "-vendor" << "apl0" << "-pix_fmt" << "yuv422p10le";
    } else if (spec.format == QLatin1String("dnxhr")) {
        // DNxHR sidesteps DNxHD's fixed raster/bitrate table.
        args << "-c:v" << "dnxhd" << "-profile:v" << "dnxhr_hqx"
             << "-pix_fmt" << "yuv422p10le";
    } else {
        args << "-c:v" << "libx264" << "-preset" << spec.preset
             << "-crf" << QString::number(spec.crf)
             << "-pix_fmt" << "yuv420p" << "-profile:v" << "high";
    }

    if (audio == QLatin1String("copy")) {
        // -shortest stops a copied track running ~20ms past the video
        args << "-c:a" << "copy" << "-shortest";
    } else if (audio == QLatin1String("aac")) {
        args << "-c:a" << "aac" << "-b:a" << "192k" << "-ar" << "48000";
    } else {
        args << "-an";
    }

    args << "-map_metadata" << "-1" << "-movflags" << "+faststart";
    args << "-progress" << "pipe:1" << "-nostats";
    args << spec.output;
    return args;
}

} // namespace sc
