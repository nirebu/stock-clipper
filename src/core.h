#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

namespace sc {

/// `out` is effectively exclusive; trimmed length is out - in.
constexpr double MIN_GAP = 0.05;

/* ------------------------------------------------------------------ */
/* edit                                                                */
/* ------------------------------------------------------------------ */

/// in/out are **source-clip** times, never times inside the cut.
/// audio: nullopt = follow settings.audio; never coerce it to false.
struct Edit {
    double in = 0.0;
    double out = 0.0;
    int rotate = 0;
    bool flipH = false;
    bool flipV = false;
    std::optional<bool> audio;

    // app-owned bookkeeping, never accepted from the UI
    std::optional<QString> outputPath;
    std::optional<double> renderedAt;
    std::optional<double> modifiedAt;

    QJsonObject toJson() const;
    static Edit fromJson(const QJsonObject &o);
    bool sameCut(const Edit &other) const;
};

/* ------------------------------------------------------------------ */
/* probe metadata                                                      */
/* ------------------------------------------------------------------ */

struct Meta {
    double duration = 0.0;
    int width = 0;
    int height = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    int rotation = 0;
    double fps = 0.0;
    QString codec;
    QString pixFmt;
    bool hasAudio = false;
    QString audioCodec;
    qint64 sizeBytes = 0;

    QJsonObject toJson() const;
    static Meta fromJson(const QJsonObject &o);
};

/* ------------------------------------------------------------------ */
/* settings                                                            */
/* ------------------------------------------------------------------ */

struct Settings {
    QString root;
    QString outputDir;
    QString suffix = QStringLiteral("-edit");
    QString format = QStringLiteral("h264");
    int crf = 18;
    QString preset = QStringLiteral("medium");
    double minDuration = 5.0;
    bool audio = false;

    QJsonObject toJson() const;
    void applyPatch(const QJsonObject &patch);
};

/// "h264", "prores" or "dnxhr".
bool isOutputFormat(const QString &format);

/* ------------------------------------------------------------------ */
/* paths + json store                                                  */
/* ------------------------------------------------------------------ */

struct Paths {
    QString dataDir;
    QString cacheDir;

    static Paths discover();
    QString settingsFile() const;
    QString editsFile() const;
    QString probeFile() const;
};

QJsonObject readJson(const QString &file, bool *ok = nullptr);
bool writeJson(const QString &file, const QJsonObject &obj);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/// Resolve a UI-supplied path and keep it inside `base`.
QString resolveInside(const QString &base, const QString &path, bool *ok, QString *error);

QString sanitizeName(const QString &s);
QString sanitizeSuffix(const QString &s);
QString cacheKey(const QString &s);
double nowMs();
double round3(double n);
double clampd(double n, double lo, double hi);

QString fmtTime(double t, int decimals = 2);
/// "1:02.50", "62.5" or "1:02:03.4" -> seconds. Returns NaN when unparseable.
double parseTime(const QString &text);

/// Cut-time normalisation; bookkeeping fields are dropped.
Edit normalizeEdit(const QJsonObject &raw, double duration);

bool isEdited(const Edit &edit, double duration);

/// A clip as the UI needs it.
struct ClipData {
    QString path;
    QString name;
    QString rel;
    QString dir;
    double mtimeMs = 0.0;
    Meta meta;
    Edit edit;
    bool edited = false;
    bool rendered = false;
    bool stale = false;
    bool loading = false;
    QString error;
    QString thumb;
};

} // namespace sc
