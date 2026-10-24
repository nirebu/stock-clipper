#include "core.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtMath>

namespace sc {

/* ------------------------------------------------------------------ */
/* Edit                                                                */
/* ------------------------------------------------------------------ */

QJsonObject Edit::toJson() const
{
    QJsonObject o;
    o.insert("in", round3(in));
    o.insert("out", round3(out));
    o.insert("rotate", rotate);
    o.insert("flipH", flipH);
    o.insert("flipV", flipV);
    o.insert("audio", audio ? QJsonValue(*audio) : QJsonValue(QJsonValue::Null));
    if (outputPath)
        o.insert("outputPath", *outputPath);
    if (renderedAt)
        o.insert("renderedAt", *renderedAt);
    if (modifiedAt)
        o.insert("modifiedAt", *modifiedAt);
    return o;
}

Edit Edit::fromJson(const QJsonObject &o)
{
    Edit e;
    e.in = o.value("in").toDouble(0.0);
    e.out = o.value("out").toDouble(0.0);
    e.rotate = o.value("rotate").toInt(0);
    e.flipH = o.value("flipH").toBool(false);
    e.flipV = o.value("flipV").toBool(false);
    if (o.value("audio").isBool())
        e.audio = o.value("audio").toBool();
    if (o.value("outputPath").isString())
        e.outputPath = o.value("outputPath").toString();
    if (o.value("renderedAt").isDouble())
        e.renderedAt = o.value("renderedAt").toDouble();
    if (o.value("modifiedAt").isDouble())
        e.modifiedAt = o.value("modifiedAt").toDouble();
    return e;
}

bool Edit::sameCut(const Edit &o) const
{
    return qFuzzyCompare(in + 1.0, o.in + 1.0)
        && qFuzzyCompare(out + 1.0, o.out + 1.0)
        && rotate == o.rotate
        && flipH == o.flipH
        && flipV == o.flipV
        && audio == o.audio;
}

/* ------------------------------------------------------------------ */
/* Meta                                                                */
/* ------------------------------------------------------------------ */

QJsonObject Meta::toJson() const
{
    return QJsonObject{
        {"duration", duration},
        {"width", width},
        {"height", height},
        {"displayWidth", displayWidth},
        {"displayHeight", displayHeight},
        {"rotation", rotation},
        {"fps", fps},
        {"codec", codec},
        {"pixFmt", pixFmt},
        {"hasAudio", hasAudio},
        {"audioCodec", audioCodec},
        {"sizeBytes", double(sizeBytes)},
    };
}

Meta Meta::fromJson(const QJsonObject &o)
{
    Meta m;
    m.duration = o.value("duration").toDouble();
    m.width = o.value("width").toInt();
    m.height = o.value("height").toInt();
    m.displayWidth = o.value("displayWidth").toInt();
    m.displayHeight = o.value("displayHeight").toInt();
    m.rotation = o.value("rotation").toInt();
    m.fps = o.value("fps").toDouble();
    m.codec = o.value("codec").toString();
    m.pixFmt = o.value("pixFmt").toString();
    m.hasAudio = o.value("hasAudio").toBool();
    m.audioCodec = o.value("audioCodec").toString();
    m.sizeBytes = qint64(o.value("sizeBytes").toDouble());
    return m;
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

QJsonObject Settings::toJson() const
{
    return QJsonObject{
        {"root", root},
        {"outputDir", outputDir},
        {"suffix", suffix},
        {"crf", crf},
        {"preset", preset},
        {"minDuration", minDuration},
        {"audio", audio},
    };
}

void Settings::applyPatch(const QJsonObject &p)
{
    auto str = [&](const char *key) -> QString {
        const QJsonValue v = p.value(key);
        return v.isString() ? v.toString() : QString();
    };
    if (!str("root").trimmed().isEmpty())
        root = QDir::cleanPath(QFileInfo(str("root").trimmed()).absoluteFilePath());
    if (!str("outputDir").trimmed().isEmpty())
        outputDir = QDir::cleanPath(QFileInfo(str("outputDir").trimmed()).absoluteFilePath());
    if (p.value("suffix").isString())
        suffix = sanitizeSuffix(p.value("suffix").toString());
    if (p.value("crf").isDouble())
        crf = qBound(0, int(p.value("crf").toDouble()), 51);
    if (p.value("preset").isString()) {
        static const QStringList presets{"ultrafast", "superfast", "veryfast", "faster", "fast",
                                         "medium", "slow", "slower", "veryslow"};
        const QString v = p.value("preset").toString();
        if (presets.contains(v))
            preset = v;
    }
    if (p.value("minDuration").isDouble())
        minDuration = qBound(0.0, p.value("minDuration").toDouble(), 120.0);
    if (p.value("audio").isBool())
        audio = p.value("audio").toBool();
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

Paths Paths::discover()
{
    Paths p;
    const QString override = qEnvironmentVariable("STOCK_HOME");
    if (!override.isEmpty()) {
        p.dataDir = QDir(override).filePath("data");
        p.cacheDir = QDir(override).filePath(".cache");
    } else {
        p.dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        p.cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    QDir().mkpath(p.dataDir);
    QDir().mkpath(p.cacheDir);

    // One-time import so an existing ./data trim set seeds a fresh state dir.
    for (const QString &name : {QStringLiteral("edits.json"), QStringLiteral("settings.json")}) {
        const QString dest = QDir(p.dataDir).filePath(name);
        if (QFileInfo::exists(dest))
            continue;
        const QString seed = QDir::current().filePath(QStringLiteral("data/") + name);
        if (QFileInfo::exists(seed))
            QFile::copy(seed, dest);
    }
    return p;
}

QString Paths::settingsFile() const { return QDir(dataDir).filePath("settings.json"); }
QString Paths::editsFile() const { return QDir(dataDir).filePath("edits.json"); }
QString Paths::probeFile() const { return QDir(cacheDir).filePath("probe.json"); }

/* ------------------------------------------------------------------ */
/* json                                                                */
/* ------------------------------------------------------------------ */

QJsonObject readJson(const QString &file, bool *ok)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    const QByteArray data = f.readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return doc.object();
}

bool writeJson(const QString &file, const QJsonObject &obj)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return f.commit();
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

QString resolveInside(const QString &base, const QString &path, bool *ok, QString *error)
{
    if (ok)
        *ok = false;
    if (path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Missing path");
        return {};
    }
    const QString baseAbs = QDir::cleanPath(QFileInfo(base).absoluteFilePath());
    const QString candidate = QDir::isAbsolutePath(path)
        ? path
        : QDir(baseAbs).filePath(path);
    const QString abs = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
    if (abs != baseAbs && !abs.startsWith(baseAbs + QLatin1Char('/'))) {
        if (error)
            *error = QStringLiteral("Path is outside the allowed folder");
        return {};
    }
    if (ok)
        *ok = true;
    return abs;
}

QString sanitizeSuffix(const QString &suffix)
{
    QString out;
    for (const QChar c : suffix.trimmed().left(40)) {
        if (!QStringLiteral("\\/:*?\"<>|").contains(c))
            out.append(c);
    }
    return out;
}

QString sanitizeName(const QString &s)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]+"));
    QString out = s;
    out.replace(bad, QStringLiteral("_"));
    out = out.trimmed();
    return out.isEmpty() ? QStringLiteral("clip") : out;
}

QString cacheKey(const QString &s)
{
    // Short stable key: 24 hex chars of the seed's SHA-1.
    const QByteArray hash = QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(hash.left(24));
}

double nowMs()
{
    return double(QDateTime::currentMSecsSinceEpoch());
}

double round3(double n) { return qRound(n * 1000.0) / 1000.0; }

double clampd(double n, double lo, double hi) { return qMin(hi, qMax(lo, n)); }

QString fmtTime(double t, int decimals)
{
    if (!std::isfinite(t) || t < 0)
        t = 0;
    const int h = int(t / 3600);
    const int m = int(std::fmod(t, 3600.0) / 60.0);
    const double s = std::fmod(t, 60.0);
    QString ss = QString::number(s, 'f', decimals);
    ss = ss.rightJustified(decimals > 0 ? 3 + decimals : 2, QLatin1Char('0'));
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(ss);
    return QStringLiteral("%1:%2").arg(m).arg(ss);
}

double parseTime(const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return qQNaN();
    static const QRegularExpression numeric(QStringLiteral("^\\d+(\\.\\d+)?$"));
    if (numeric.match(s).hasMatch())
        return s.toDouble();
    const QStringList parts = s.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3)
        return qQNaN();
    double total = 0;
    for (const QString &p : parts) {
        bool ok = false;
        const double v = p.toDouble(&ok);
        if (!ok)
            return qQNaN();
        total = total * 60.0 + v;
    }
    return total;
}

Edit normalizeEdit(const QJsonObject &raw, double duration)
{
    const double dur = duration > 0 ? duration : MIN_GAP;
    double inP = raw.value("in").toDouble(0.0);
    double outP = raw.value("out").isDouble() ? raw.value("out").toDouble() : dur;
    if (!std::isfinite(inP))
        inP = 0.0;
    if (!std::isfinite(outP))
        outP = dur;
    inP = clampd(inP, 0.0, qMax(0.0, dur - MIN_GAP));
    outP = clampd(outP, inP + MIN_GAP, dur);

    Edit e;
    e.in = round3(inP);
    e.out = round3(outP);
    const int rot = raw.value("rotate").toInt(0);
    e.rotate = (rot == 90 || rot == 180 || rot == 270) ? rot : 0;
    e.flipH = raw.value("flipH").toBool(false);
    e.flipV = raw.value("flipV").toBool(false);
    if (raw.value("audio").isBool())
        e.audio = raw.value("audio").toBool();
    return e;
}

bool isEdited(const Edit &e, double duration)
{
    return e.in > 0.002
        || e.out < duration - 0.002
        || e.rotate != 0
        || e.flipH
        || e.flipV;
}

} // namespace sc
