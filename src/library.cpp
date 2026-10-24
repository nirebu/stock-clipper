#include "library.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#include "media.h"

namespace sc {

namespace {
constexpr int kProbeConcurrency = 4;
constexpr int kThumbConcurrency = 3;

QString orientTag(int rotate, bool flipH, bool flipV)
{
    return QStringLiteral("%1-%2-%3").arg(rotate).arg(flipH ? 1 : 0).arg(flipV ? 1 : 0);
}
} // namespace

/* ------------------------------------------------------------------ */
/* scan                                                                */
/* ------------------------------------------------------------------ */

static void walk(const QDir &dir, int depth, QStringList &out)
{
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                    QDir::Name);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (name.startsWith(QLatin1Char('.')))
            continue;
        if (fi.isDir()) {
            if (depth < 3)
                walk(QDir(fi.absoluteFilePath()), depth + 1, out);
        } else if (isVideoFile(name)) {
            out << fi.absoluteFilePath();
        }
    }
}

QStringList listVideos(const QString &root)
{
    QStringList out;
    QDir dir(root);
    if (!dir.exists())
        return out;
    walk(dir, 0, out);
    return out;
}

/* ------------------------------------------------------------------ */
/* Prober                                                              */
/* ------------------------------------------------------------------ */

Prober::Prober(QObject *parent) : QObject(parent) {}

void Prober::request(const QString &path)
{
    if (m_inflight.contains(path) || m_pending.contains(path))
        return;
    m_pending.append(path);
    pump();
}

void Prober::pump()
{
    while (m_active.size() < kProbeConcurrency && !m_pending.isEmpty()) {
        auto *job = new Job;
        job->path = m_pending.takeFirst();
        job->proc = new QProcess(this);
        job->proc->setProgram(QStringLiteral("ffprobe"));
        job->proc->setArguments(probeArgs(job->path));
        m_inflight.insert(job->path);
        m_active.append(job);

        connect(job->proc, &QProcess::readyReadStandardOutput, this,
                [job] { job->out += job->proc->readAllStandardOutput(); });

        connect(job->proc, &QProcess::finished, this,
                [this, job](int code, QProcess::ExitStatus status) {
                    job->out += job->proc->readAllStandardOutput();
                    const QByteArray err = job->proc->readAllStandardError();
                    const QString path = job->path;
                    m_active.removeAll(job);
                    m_inflight.remove(path);

                    Meta meta;
                    QString error;
                    if (code == 0 && status == QProcess::NormalExit) {
                        meta = parseProbe(job->out, &error);
                    } else {
                        error = QString::fromUtf8(err).trimmed();
                        if (error.isEmpty())
                            error = QStringLiteral("ffprobe failed");
                    }

                    job->proc->deleteLater();
                    delete job;

                    emit probed(path, meta, error);
                    pump();
                });

        job->proc->start();
    }
}

/* ------------------------------------------------------------------ */
/* Thumbnailer                                                         */
/* ------------------------------------------------------------------ */

Thumbnailer::Thumbnailer(const QString &cacheDir, QObject *parent)
    : QObject(parent), m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);
}

QString Thumbnailer::outFor(const QString &path, const QString &kind, int rotate, bool flipH,
                            bool flipV, double mtimeMs, qint64 size) const
{
    const QString seed = path + QLatin1Char('|') + QString::number(qint64(mtimeMs)) + QLatin1Char('|')
        + QString::number(size) + QLatin1Char('|') + kind + QLatin1Char('|')
        + orientTag(rotate, flipH, flipV);
    return QDir(m_cacheDir).filePath(kind + QLatin1Char('-') + cacheKey(seed) + QStringLiteral(".jpg"));
}

QString Thumbnailer::pathFor(const QString &path, const QString &kind, double duration, int rotate,
                             bool flipH, bool flipV, double mtimeMs, qint64 size) const
{
    Q_UNUSED(duration);
    return outFor(path, kind, rotate, flipH, flipV, mtimeMs, size);
}

QString Thumbnailer::cached(const QString &path, const QString &kind, double duration, int rotate,
                            bool flipH, bool flipV, double mtimeMs, qint64 size) const
{
    Q_UNUSED(duration);
    const QString out = outFor(path, kind, rotate, flipH, flipV, mtimeMs, size);
    return QFileInfo::exists(out) ? out : QString();
}

void Thumbnailer::request(const QString &path, const QString &kind, double duration, int rotate,
                          bool flipH, bool flipV, double mtimeMs, qint64 size)
{
    const QString out = outFor(path, kind, rotate, flipH, flipV, mtimeMs, size);
    if (QFileInfo::exists(out)) {
        emit ready(kind, path, out);
        return;
    }
    if (m_inflight.contains(out))
        return;
    for (const Req &r : m_pending) {
        if (r.out == out)
            return;
    }

    Req req;
    req.path = path;
    req.kind = kind;
    req.out = out;
    req.duration = duration;
    req.rotate = rotate;
    req.flipH = flipH;
    req.flipV = flipV;
    m_pending.append(req);
    pump();
}

void Thumbnailer::pump()
{
    while (m_active < kThumbConcurrency && !m_pending.isEmpty()) {
        const Req req = m_pending.takeFirst();
        m_inflight.insert(req.out);
        ++m_active;

        const QString orientation = buildVideoFilter(req.rotate, req.flipH, req.flipV);
        QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-y"};

        if (req.kind == QLatin1String("thumb")) {
            const double at = clampd((req.duration > 0 ? req.duration : 2.0) * 0.15, 0.0, 1.5);
            args << "-ss" << QString::number(at, 'f', 3);
        }
        args << "-i" << req.path;
        if (req.kind == QLatin1String("thumb")) {
            args << "-frames:v" << "1" << "-vf" << (orientation + QStringLiteral(",scale=480:-2"));
            args << "-q:v" << "4";
        } else {
            // one tiled strip of ~10 upright frames spread across the clip
            const double fps = qMin(30.0, qMax(0.02, 10.0 / qMax(req.duration, 0.2)));
            args << "-vf" << (orientation + QStringLiteral(",fps=%1,scale=260:-2,crop=260:146,tile=10x1")
                                           .arg(fps, 0, 'f', 6));
            args << "-frames:v" << "1" << "-q:v" << "5" << "-an" << "-sn";
        }
        args << req.out;

        auto *proc = new QProcess(this);
        proc->setProgram(QStringLiteral("ffmpeg"));
        proc->setArguments(args);

        connect(proc, &QProcess::finished, this,
                [this, req, proc](int code, QProcess::ExitStatus status) {
                    --m_active;
                    m_inflight.remove(req.out);
                    proc->deleteLater();
                    if (code == 0 && status == QProcess::NormalExit && QFileInfo::exists(req.out))
                        emit ready(req.kind, req.path, req.out);
                    else
                        emit failed(req.kind, req.path);
                    pump();
                });

        proc->start();
    }
}

/* ------------------------------------------------------------------ */
/* ClipModel                                                           */
/* ------------------------------------------------------------------ */

ClipModel::ClipModel(QObject *parent) : QAbstractListModel(parent) {}

int ClipModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_clips.size();
}

int ClipModel::rowOf(const QString &path) const
{
    for (int i = 0; i < m_clips.size(); ++i) {
        if (m_clips.at(i).path == path)
            return i;
    }
    return -1;
}

const ClipData *ClipModel::find(const QString &path) const
{
    const int row = rowOf(path);
    return row < 0 ? nullptr : &m_clips.at(row);
}

QVariantList ClipModel::flagsFor(const ClipData &c) const
{
    QVariantList out;
    auto add = [&out](const QString &text, const QString &kind) {
        QVariantMap m;
        m.insert("text", text);
        m.insert("kind", kind);
        out.append(m);
    };

    if (!c.error.isEmpty())
        add(QStringLiteral("unreadable"), QStringLiteral("err"));
    if (c.edit.rotate != 0)
        add(QStringLiteral("%1°").arg(c.edit.rotate), QStringLiteral("rot"));
    if (c.edit.flipH || c.edit.flipV)
        add(QStringLiteral("flip"), QStringLiteral("rot"));
    if (c.edited)
        add(QStringLiteral("trimmed"), QStringLiteral("trim"));
    if (c.rendered)
        add(c.stale ? QStringLiteral("re-render") : QStringLiteral("rendered"),
            c.stale ? QStringLiteral("warn") : QStringLiteral("ok"));
    if (out.isEmpty() && c.meta.displayHeight > c.meta.displayWidth)
        add(QStringLiteral("vertical"), QStringLiteral("plain"));
    return out;
}

QVariant ClipModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return {};
    const ClipData &c = m_clips.at(index.row());

    switch (role) {
    case PathRole:
        return c.path;
    case NameRole:
        return c.name;
    case RelRole:
        return c.rel;
    case DurationRole:
        return c.meta.duration;
    case DisplayWidthRole:
        return c.meta.displayWidth;
    case DisplayHeightRole:
        return c.meta.displayHeight;
    case FpsRole:
        return c.meta.fps;
    case ThumbRole:
        return c.thumb;
    case FlagsRole:
        return flagsFor(c);
    case ErrorRole:
        return c.error;
    case EditedRole:
        return c.edited;
    case RenderedRole:
        return c.rendered;
    case StaleRole:
        return c.stale;
    case RotateRole:
        return c.edit.rotate;
    case FlipHRole:
        return c.edit.flipH;
    case FlipVRole:
        return c.edit.flipV;
    case HasAudioRole:
        return c.meta.hasAudio;
    case ActiveRole:
        return c.path == m_active;
    case LoadingRole:
        return c.loading;
    default:
        return {};
    }
}

QHash<int, QByteArray> ClipModel::roleNames() const
{
    return {
        {PathRole, "path"},
        {NameRole, "name"},
        {RelRole, "rel"},
        {DurationRole, "duration"},
        {DisplayWidthRole, "displayWidth"},
        {DisplayHeightRole, "displayHeight"},
        {FpsRole, "fps"},
        {ThumbRole, "thumb"},
        {FlagsRole, "flags"},
        {ErrorRole, "error"},
        {EditedRole, "edited"},
        {RenderedRole, "rendered"},
        {StaleRole, "stale"},
        {RotateRole, "rotate"},
        {FlipHRole, "flipH"},
        {FlipVRole, "flipV"},
        {HasAudioRole, "hasAudio"},
        {ActiveRole, "active"},
        {LoadingRole, "loading"},
    };
}

void ClipModel::setClips(QVector<ClipData> clips)
{
    beginResetModel();
    m_clips = std::move(clips);
    endResetModel();
}

void ClipModel::upsert(const ClipData &clip)
{
    const int row = rowOf(clip.path);
    if (row < 0) {
        beginInsertRows({}, m_clips.size(), m_clips.size());
        m_clips.append(clip);
        endInsertRows();
        return;
    }
    m_clips[row] = clip;
    emitRow(row);
}

void ClipModel::setActive(const QString &path)
{
    const int oldRow = rowOf(m_active);
    m_active = path;
    if (oldRow >= 0)
        emitRow(oldRow);
    const int newRow = rowOf(path);
    if (newRow >= 0 && newRow != oldRow)
        emitRow(newRow);
}

void ClipModel::update(const QString &path, const std::function<void(ClipData &)> &fn)
{
    const int row = rowOf(path);
    if (row < 0)
        return;
    fn(m_clips[row]);
    emitRow(row);
}

void ClipModel::emitRow(int row)
{
    if (row < 0 || row >= m_clips.size())
        return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
}

QVector<ClipData> ClipModel::editedClips() const
{
    QVector<ClipData> out;
    for (const ClipData &c : m_clips) {
        if (c.edited && c.error.isEmpty())
            out.append(c);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* ClipFilterModel                                                     */
/* ------------------------------------------------------------------ */

ClipFilterModel::ClipFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void ClipFilterModel::setFilter(const QString &f)
{
    if (m_filter == f)
        return;
    m_filter = f;
    invalidateFilter();
    emit filterChanged();
}

int ClipFilterModel::sourceRow(int proxyRow) const
{
    const QModelIndex idx = index(proxyRow, 0);
    return idx.isValid() ? mapToSource(idx).row() : -1;
}

bool ClipFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_filter.trimmed().isEmpty())
        return true;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString rel = sourceModel()->data(idx, ClipModel::RelRole).toString();
    return rel.contains(m_filter.trimmed(), Qt::CaseInsensitive);
}

} // namespace sc
