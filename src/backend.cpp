#include "backend.h"

#include <QAudioOutput>
#include <QDesktopServices>
#include <QDir>
#include <cstdio>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMath>

#include "media.h"

namespace sc {

namespace {
QString fileUrl(const QString &path)
{
    return QUrl::fromLocalFile(path).toString();
}
} // namespace

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_paths = Paths::discover();

    m_model = new ClipModel(this);
    m_filter = new ClipFilterModel(this);
    m_filter->setSourceModel(m_model);
    m_queue = new RenderQueue(this);
    m_prober = new Prober(this);
    m_thumbs = new Thumbnailer(m_paths.cacheDir, this);

    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(1.0f);
    m_player->setAudioOutput(m_audio);

    m_tick.setTimerType(Qt::PreciseTimer);
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, &Backend::tick);

    m_save.setSingleShot(true);
    m_save.setInterval(220);
    connect(&m_save, &QTimer::timeout, this, &Backend::saveEdits);

    m_stripTimer.setSingleShot(true);
    m_stripTimer.setInterval(350);
    connect(&m_stripTimer, &QTimer::timeout, this, &Backend::loadStrip);

    connect(m_prober, &Prober::probed, this,
            [this](const QString &path, const Meta &meta, const QString &error) {
                QFileInfo fi(path);
                ProbeEntry entry;
                entry.mtimeMs = double(fi.lastModified().toMSecsSinceEpoch());
                entry.size = fi.size();
                entry.meta = meta;
                if (error.isEmpty())
                    m_probe.insert(path, entry);
                saveProbeCache();

                m_model->update(path, [&](ClipData &c) {
                    c.loading = false;
                    c.error = error;
                    if (error.isEmpty())
                        c.meta = meta;
                    decorate(c, true);
                });
                emit libraryChanged();
                updateThumb(path);
                if (path == m_currentPath)
                    syncCurrent();
            });

    connect(m_thumbs, &Thumbnailer::ready, this,
            [this](const QString &kind, const QString &path, const QString &file) {
                if (kind == QLatin1String("thumb")) {
                    const QString url = fileUrl(file);
                    m_model->update(path, [&](ClipData &c) { c.thumb = url; });
                } else if (path == m_currentPath && file == m_stripWant) {
                    m_stripLoading = false;
                    m_stripUrl = fileUrl(file);
                    emit stripChanged();
                }
            });

    connect(m_thumbs, &Thumbnailer::failed, this,
            [this](const QString &kind, const QString &path) {
                if (kind == QLatin1String("strip") && path == m_currentPath) {
                    m_stripLoading = false;
                    emit stripChanged();
                }
            });

    connect(m_queue, &RenderQueue::jobFinished, this,
            [this](const Job &job, bool ok) {
                if (ok)
                    persistJobResult(job);
                refreshAllRows();
                if (job.path == m_currentPath)
                    emit currentChanged();
            });
    connect(m_queue, &QAbstractItemModel::dataChanged, this, [this] { emit queueChanged(); });
    connect(m_queue, &QAbstractItemModel::rowsInserted, this, [this] { emit queueChanged(); });

    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                // priming runs the pipeline just long enough to emit one frame
                if (m_priming)
                    return;
                if (!qEnvironmentVariableIsEmpty("SC_DEBUG"))
                    std::fprintf(stderr, "[player] state=%d pos=%lld\n", int(state),
                                 m_player->position());
                const bool playing = state == QMediaPlayer::PlayingState;
                if (playing)
                    m_tick.start();
                else
                    m_tick.stop();
                if (playing != m_playing) {
                    m_playing = playing;
                    emit playbackChanged();
                }
            });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (!qEnvironmentVariableIsEmpty("SC_DEBUG"))
                    std::fprintf(stderr, "[player] status=%d pendingSeek=%.0f pos=%lld\n",
                                 int(status), m_pendingSeekMs, m_player->position());
                if (status == QMediaPlayer::LoadedMedia && m_pendingSeekMs >= 0) {
                    m_player->setPosition(qint64(m_pendingSeekMs));
                    m_playheadMs = m_pendingSeekMs;
                    m_pendingSeekMs = -1;
                    emit playbackChanged();
                    primeFrame();
                }
            });

    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) {
                if (!message.isEmpty())
                    emit toast(QStringLiteral("Playback: %1").arg(message), QStringLiteral("error"));
            });

    loadState();
    rescan();
}

Backend::~Backend()
{
    flushSave();
}

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

void Backend::loadState()
{
    const bool haveSettings = QFileInfo::exists(m_paths.settingsFile());
    bool ok = false;
    QJsonObject s = readJson(m_paths.settingsFile(), &ok);
    if (ok)
        m_settings.applyPatch(s);

    // First run from the checkout: use its input/ and renders/ folders. Otherwise
    // fall back to a neutral pair of folders in $HOME.
    if (m_settings.root.trimmed().isEmpty() || m_settings.outputDir.trimmed().isEmpty()) {
        const QString cwdInput = QDir::current().filePath(QStringLiteral("input"));
        if (!haveSettings && QFileInfo(cwdInput).isDir()) {
            m_settings.root = QDir(cwdInput).absolutePath();
            m_settings.outputDir = QDir::current().filePath(QStringLiteral("renders"));
        } else {
            if (m_settings.root.trimmed().isEmpty())
                m_settings.root = QDir::home().filePath(QStringLiteral("stock-input"));
            if (m_settings.outputDir.trimmed().isEmpty())
                m_settings.outputDir = QDir::home().filePath(QStringLiteral("stock-renders"));
        }
    }

    QDir().mkpath(m_settings.root);
    QDir().mkpath(m_settings.outputDir);
    if (!ok)
        writeJson(m_paths.settingsFile(), m_settings.toJson());

    ok = false;
    const QJsonObject edits = readJson(m_paths.editsFile(), &ok);
    if (ok) {
        for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
            if (it.value().isObject())
                m_edits.insert(it.key(), Edit::fromJson(it.value().toObject()));
        }
    }

    ok = false;
    const QJsonObject probes = readJson(m_paths.probeFile(), &ok);
    if (ok) {
        for (auto it = probes.constBegin(); it != probes.constEnd(); ++it) {
            const QJsonObject e = it.value().toObject();
            ProbeEntry entry;
            entry.mtimeMs = e.value(QStringLiteral("mtimeMs")).toDouble();
            entry.size = qint64(e.value(QStringLiteral("size")).toDouble());
            entry.meta = Meta::fromJson(e.value(QStringLiteral("meta")).toObject());
            m_probe.insert(it.key(), entry);
        }
    }

    m_ffmpegOk = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
    if (!m_ffmpegOk)
        emit toast(QStringLiteral("ffmpeg / ffprobe were not found in PATH"), QStringLiteral("error"));

    emit settingsChanged();
}

void Backend::saveEdits()
{
    QJsonObject root;
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it)
        root.insert(it.key(), it.value().toJson());
    writeJson(m_paths.editsFile(), root);
    m_dirty = false;
}

void Backend::saveProbeCache()
{
    QJsonObject root;
    for (auto it = m_probe.constBegin(); it != m_probe.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("mtimeMs"), it->mtimeMs);
        entry.insert(QStringLiteral("size"), double(it->size));
        entry.insert(QStringLiteral("meta"), it->meta.toJson());
        root.insert(it.key(), entry);
    }
    writeJson(m_paths.probeFile(), root);
}

void Backend::flushSave()
{
    if (m_save.isActive())
        m_save.stop();
    if (m_dirty)
        saveEdits();
}

/* ------------------------------------------------------------------ */
/* library                                                             */
/* ------------------------------------------------------------------ */

void Backend::rescan()
{
    if (m_settings.root.isEmpty())
        return;

    m_scanning = true;
    emit libraryChanged();

    const QString root = m_settings.root;
    const QStringList files = listVideos(root);
    QVector<ClipData> clips;
    clips.reserve(files.size());
    QStringList needProbe;

    for (const QString &file : files) {
        const QFileInfo fi(file);
        ClipData c;
        c.path = file;
        c.name = fi.fileName();
        c.dir = fi.absolutePath();
        c.rel = QDir(root).relativeFilePath(file);
        c.mtimeMs = double(fi.lastModified().toMSecsSinceEpoch());

        const auto it = m_probe.constFind(file);
        const bool fresh = it != m_probe.constEnd() && it->mtimeMs == c.mtimeMs
            && it->size == fi.size();
        if (fresh) {
            c.meta = it->meta;
        } else {
            c.loading = true;
            needProbe << file;
        }
        decorate(c, false);
        clips.append(c);
    }

    m_model->setClips(clips);
    m_scanning = false;
    emit libraryChanged();

    for (const QString &p : needProbe)
        m_prober->request(p);
    for (const ClipData &c : m_model->all()) {
        if (!c.loading && c.error.isEmpty() && c.thumb.isEmpty())
            updateThumb(c.path);
    }

    const bool stillThere = !m_currentPath.isEmpty() && m_model->find(m_currentPath) != nullptr;
    if (stillThere) {
        syncCurrent();
    } else if (!m_currentPath.isEmpty()) {
        m_currentPath.clear();
        m_viewingRender = false;
        m_renderedName.clear();
        m_model->setActive({});
        m_player->stop();
        m_player->setSource(QUrl());
        m_stripUrl.clear();
        m_stripWant.clear();
        m_stripLoading = false;
        emit stripChanged();
        emit currentChanged();
        emit editChanged();
    }
}

void Backend::decorate(ClipData &c, bool requestPreview)
{
    if (c.loading)
        return;

    if (!c.error.isEmpty()) {
        c.edited = false;
        c.rendered = false;
        c.stale = false;
        return;
    }

    const bool stored = m_edits.contains(c.path);
    const Edit saved = m_edits.value(c.path);

    QJsonObject raw;
    if (stored) {
        raw.insert(QStringLiteral("in"), saved.in);
        raw.insert(QStringLiteral("out"), saved.out);
        raw.insert(QStringLiteral("rotate"), saved.rotate);
        raw.insert(QStringLiteral("flipH"), saved.flipH);
        raw.insert(QStringLiteral("flipV"), saved.flipV);
        raw.insert(QStringLiteral("audio"),
                   saved.audio ? QJsonValue(*saved.audio) : QJsonValue(QJsonValue::Null));
    }

    Edit e = normalizeEdit(raw, c.meta.duration);
    if (stored) {
        e.outputPath = saved.outputPath;
        e.renderedAt = saved.renderedAt;
        e.modifiedAt = saved.modifiedAt;
    }

    c.edit = e;
    c.edited = isEdited(e, c.meta.duration);
    c.rendered = e.outputPath.has_value() && QFileInfo::exists(*e.outputPath);
    c.stale = c.rendered && e.modifiedAt.value_or(0.0) > e.renderedAt.value_or(0.0);

    if (requestPreview && c.thumb.isEmpty()) {
        const QString hit = m_thumbs->cached(c.path, QStringLiteral("thumb"), c.meta.duration,
                                            e.rotate, e.flipH, e.flipV, c.mtimeMs, c.meta.sizeBytes);
        if (!hit.isEmpty()) {
            c.thumb = fileUrl(hit);
        } else {
            m_thumbs->request(c.path, QStringLiteral("thumb"), c.meta.duration, e.rotate, e.flipH,
                              e.flipV, c.mtimeMs, c.meta.sizeBytes);
        }
    }
}

void Backend::updateThumb(const QString &path)
{
    const ClipData *c = m_model->find(path);
    if (!c || c->loading || !c->error.isEmpty())
        return;

    const QString hit = m_thumbs->cached(path, QStringLiteral("thumb"), c->meta.duration,
                                        c->edit.rotate, c->edit.flipH, c->edit.flipV,
                                        c->mtimeMs, c->meta.sizeBytes);
    if (!hit.isEmpty()) {
        const QString url = fileUrl(hit);
        if (c->thumb != url)
            m_model->update(path, [&](ClipData &cd) { cd.thumb = url; });
        return;
    }
    m_thumbs->request(path, QStringLiteral("thumb"), c->meta.duration, c->edit.rotate,
                      c->edit.flipH, c->edit.flipV, c->mtimeMs, c->meta.sizeBytes);
}

void Backend::refreshAllRows()
{
    QStringList paths;
    for (const ClipData &c : m_model->all())
        paths << c.path;
    for (const QString &p : paths)
        m_model->update(p, [this](ClipData &c) { decorate(c, false); });
    emit libraryChanged();
}

/* ------------------------------------------------------------------ */
/* selection                                                           */
/* ------------------------------------------------------------------ */

const ClipData *Backend::current() const
{
    return m_currentPath.isEmpty() ? nullptr : m_model->find(m_currentPath);
}

void Backend::selectClip(const QString &path)
{
    const ClipData *c = m_model->find(path);
    if (!c)
        return;

    const bool same = path == m_currentPath && !m_viewingRender;
    m_currentPath = path;
    m_viewingRender = false;
    m_renderedName.clear();
    m_model->setActive(path);

    m_priming = false;
    m_wantPlay = false;
    m_audio->setMuted(false);
    m_player->pause();
    if (!c->loading && c->error.isEmpty() && !same) {
        m_player->setSource(QUrl::fromLocalFile(path));
        m_pendingSeekMs = c->edit.in * 1000.0;
    }
    m_playheadMs = c->edit.in * 1000.0;
    m_stripTimer.start();

    emit currentChanged();
    emit editChanged();
    emit playbackChanged();
}

void Backend::selectSourceRow(int sourceRow)
{
    const ClipData *c = m_model->find(
        sourceRow >= 0 && sourceRow < m_model->rowCount()
            ? m_model->data(m_model->index(sourceRow, 0), ClipModel::PathRole).toString()
            : QString());
    if (c)
        selectClip(c->path);
}

void Backend::syncCurrent()
{
    const ClipData *c = current();
    if (c && !c->loading && c->error.isEmpty() && !m_viewingRender
        && m_player->source().toLocalFile() != m_currentPath) {
        m_player->setSource(QUrl::fromLocalFile(m_currentPath));
        m_pendingSeekMs = c->edit.in * 1000.0;
    }
    emit currentChanged();
    emit editChanged();
    m_stripTimer.start();
}

void Backend::setFilter(const QString &f)
{
    m_filter->setFilter(f);
    emit libraryChanged();
}

QString Backend::filter() const
{
    return m_filter->filter();
}

/* ------------------------------------------------------------------ */
/* edit                                                                */
/* ------------------------------------------------------------------ */

void Backend::applyEditTo(const QString &path, const std::function<void(Edit &)> &mutate)
{
    const ClipData *c = m_model->find(path);
    if (!c || c->loading || !c->error.isEmpty())
        return;

    const Edit before = c->edit;
    Edit next = before;
    mutate(next);

    QJsonObject raw;
    raw.insert(QStringLiteral("in"), next.in);
    raw.insert(QStringLiteral("out"), next.out);
    raw.insert(QStringLiteral("rotate"), next.rotate);
    raw.insert(QStringLiteral("flipH"), next.flipH);
    raw.insert(QStringLiteral("flipV"), next.flipV);
    raw.insert(QStringLiteral("audio"),
               next.audio ? QJsonValue(*next.audio) : QJsonValue(QJsonValue::Null));

    Edit norm = normalizeEdit(raw, c->meta.duration);
    if (norm.sameCut(before))
        return;

    norm.outputPath = before.outputPath;
    norm.renderedAt = before.renderedAt;
    norm.modifiedAt = nowMs();
    m_edits.insert(path, norm);

    m_model->update(path, [&](ClipData &cd) {
        cd.edit = norm;
        cd.edited = isEdited(norm, cd.meta.duration);
        cd.stale = cd.rendered && norm.modifiedAt.value_or(0.0) > norm.renderedAt.value_or(0.0);
    });

    m_dirty = true;
    m_save.start();

    if (path == m_currentPath) {
        emit editChanged();
        emit currentChanged();
        m_stripTimer.start();
    }
    emit libraryChanged();
}

void Backend::applyEdit(const std::function<void(Edit &)> &mutate, const QString &seek)
{
    if (m_currentPath.isEmpty() || m_viewingRender)
        return;
    applyEditTo(m_currentPath, mutate);

    if (!seek.isEmpty()) {
        const ClipData *c = current();
        if (c) {
            if (seek == QLatin1String("in"))
                seekTo(c->edit.in);
            else
                seekTo(qMax(c->edit.in, c->edit.out - 0.001));
        }
    }
}

void Backend::setIn(double t)
{
    applyEdit([t](Edit &e) { e.in = t; }, QStringLiteral("in"));
}

void Backend::setOut(double t)
{
    applyEdit([t](Edit &e) { e.out = t; }, QStringLiteral("out"));
}

void Backend::nudgeIn(double delta)
{
    const ClipData *c = current();
    if (!c)
        return;
    const double t = c->edit.in + delta;
    applyEdit([t](Edit &e) { e.in = t; }, QStringLiteral("in"));
}

void Backend::nudgeOut(double delta)
{
    const ClipData *c = current();
    if (!c)
        return;
    const double t = c->edit.out + delta;
    applyEdit([t](Edit &e) { e.out = t; }, QStringLiteral("out"));
}

void Backend::setRotate(int r)
{
    applyEdit([r](Edit &e) { e.rotate = normRotation(r); });
}

void Backend::toggleFlipH()
{
    applyEdit([](Edit &e) { e.flipH = !e.flipH; });
}

void Backend::toggleFlipV()
{
    applyEdit([](Edit &e) { e.flipV = !e.flipV; });
}

void Backend::rotateRow(const QString &path)
{
    const ClipData *c = m_model->find(path);
    if (!c || c->loading || !c->error.isEmpty())
        return;
    const int next = (c->edit.rotate + 90) % 360;
    applyEditTo(path, [next](Edit &e) { e.rotate = next; });
    updateThumb(path);
    if (path == m_currentPath)
        m_stripTimer.start();
}

void Backend::setAudioChoice(bool on)
{
    applyEdit([on](Edit &e) { e.audio = on; });
}

void Backend::resetEdit()
{
    const ClipData *c = current();
    if (!c)
        return;
    const double duration = c->meta.duration;
    applyEdit(
        [duration](Edit &e) {
            e.in = 0.0;
            e.out = duration;
            e.rotate = 0;
            e.flipH = false;
            e.flipV = false;
            e.audio.reset();
        },
        QStringLiteral("in"));
}

void Backend::commitFieldText(const QString &which, const QString &text)
{
    const double t = parseTime(text);
    if (!std::isfinite(t)) {
        emit editChanged();
        return;
    }
    if (which == QLatin1String("in"))
        setIn(t);
    else if (which == QLatin1String("out"))
        setOut(t);
}

void Backend::loadStrip()
{
    const ClipData *c = current();
    if (!c || c->loading || !c->error.isEmpty()) {
        if (!m_stripUrl.isEmpty() || !m_stripWant.isEmpty()) {
            m_stripUrl.clear();
            m_stripWant.clear();
            m_stripLoading = false;
            emit stripChanged();
        }
        return;
    }

    const QString want = m_thumbs->pathFor(c->path, QStringLiteral("strip"), c->meta.duration,
                                          c->edit.rotate, c->edit.flipH, c->edit.flipV,
                                          c->mtimeMs, c->meta.sizeBytes);
    if (want == m_stripWant && !m_stripUrl.isEmpty())
        return;

    m_stripWant = want;
    m_stripLoading = !QFileInfo::exists(want);
    emit stripChanged();
    m_thumbs->request(c->path, QStringLiteral("strip"), c->meta.duration, c->edit.rotate,
                      c->edit.flipH, c->edit.flipV, c->mtimeMs, c->meta.sizeBytes);
}

/* ------------------------------------------------------------------ */
/* playback                                                            */
/* ------------------------------------------------------------------ */

void Backend::attachVideoSink(QObject *sink)
{
    if (!qEnvironmentVariableIsEmpty("SC_DEBUG"))
        std::fprintf(stderr, "[sink] attach %p\n", static_cast<void *>(sink));
    auto *videoSink = qobject_cast<QVideoSink *>(sink);
    if (!videoSink)
        return;
    m_player->setVideoSink(videoSink);
    QObject::connect(videoSink, &QVideoSink::videoFrameChanged, this,
                     [this](const QVideoFrame &) {
                         if (!m_priming)
                             return;
                         m_priming = false;
                         m_player->pause();
                         m_audio->setMuted(false);
                     });
    if (!qEnvironmentVariableIsEmpty("SC_DEBUG")) {
        QObject::connect(videoSink, &QVideoSink::videoFrameChanged, videoSink,
                         [](const QVideoFrame &frame) {
                             static int n = 0;
                             ++n;
                             if (n <= 3 || n % 50 == 0)
                                 std::fprintf(stderr, "[sink] frame %d %dx%d valid=%d\n", n,
                                              frame.width(), frame.height(), frame.isValid());
                         });
    }
}

void Backend::primeFrame()
{
    // A paused QMediaPlayer emits no frames at all, so the stage would stay
    // black until the first play. Run the pipeline just long enough for one
    // frame, with the audio muted so priming is silent.
    if (m_playing || m_priming || m_viewingRender || m_wantPlay)
        return;
    if (m_player->source().isEmpty() || m_player->mediaStatus() < QMediaPlayer::LoadedMedia)
        return;
    m_priming = true;
    m_audio->setMuted(true);
    m_player->play();
}

void Backend::togglePlay()
{
    if (m_currentPath.isEmpty())
        return;

    if (m_priming) {
        // the user pressed play while the first frame was being primed
        m_priming = false;
        m_audio->setMuted(false);
        m_wantPlay = true;
        return;
    }

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_wantPlay = false;
        m_player->pause();
        return;
    }

    m_wantPlay = true;

    const ClipData *c = current();
    if (!c)
        return;

    if (!m_viewingRender && !c->loading) {
        const qint64 pos = m_player->position();
        const qint64 inMs = qint64(c->edit.in * 1000.0);
        const qint64 outMs = qint64(c->edit.out * 1000.0);
        if (pos < inMs - 20 || pos >= outMs - 30) {
            m_player->setPosition(inMs);
            m_playheadMs = double(inMs);
        }
    }
    m_player->play();
}

void Backend::seekTo(double seconds)
{
    if (m_currentPath.isEmpty())
        return;

    double limit = 0.0;
    const ClipData *c = current();
    if (c && !m_viewingRender)
        limit = c->meta.duration;
    else if (m_player->duration() > 0)
        limit = m_player->duration() / 1000.0;

    const double t = clampd(seconds, 0.0, limit > 0 ? limit : seconds);
    const qint64 ms = qint64(t * 1000.0);
    m_player->setPosition(ms);
    m_playheadMs = double(ms);
    emit playbackChanged();
    primeFrame();
}

void Backend::stepFrame(int dir, bool big)
{
    const ClipData *c = current();
    if (!c)
        return;
    m_player->pause();
    const double step = big ? 1.0 : (c->meta.fps > 0 ? 1.0 / c->meta.fps : 0.04);
    seekTo(m_playheadMs / 1000.0 + dir * step);
}

void Backend::setInAtPlayhead()
{
    if (m_currentPath.isEmpty() || m_viewingRender)
        return;
    const double t = m_playheadMs / 1000.0;
    applyEdit([t](Edit &e) { e.in = t; });
}

void Backend::setOutAtPlayhead()
{
    if (m_currentPath.isEmpty() || m_viewingRender)
        return;
    const double t = m_playheadMs / 1000.0;
    applyEdit([t](Edit &e) { e.out = t; });
}

void Backend::setLoop(bool on)
{
    m_loop = on;
}

void Backend::playRendered(const QString &outPath)
{
    if (!QFileInfo::exists(outPath))
        return;
    m_viewingRender = true;
    m_renderedName = QFileInfo(outPath).fileName();
    m_priming = false;
    m_wantPlay = true;
    m_audio->setMuted(false);
    m_player->setSource(QUrl::fromLocalFile(outPath));
    m_player->setLoops(QMediaPlayer::Infinite);
    m_player->play();
    emit currentChanged();
    emit playbackChanged();
}

void Backend::backToSource()
{
    m_player->setLoops(1);
    m_viewingRender = false;
    m_renderedName.clear();
    if (!m_currentPath.isEmpty())
        selectClip(m_currentPath);
    else
        emit currentChanged();
}

void Backend::tick()
{
    const ClipData *c = current();
    if (!m_viewingRender && c && !c->loading && !m_seekGuard) {
        const qint64 pos = m_player->position();
        const qint64 outMs = qint64(c->edit.out * 1000.0);
        const qint64 inMs = qint64(c->edit.in * 1000.0);
        // the out point is enforced here, not by timeupdate: 16 ms resolution
        if (pos >= outMs - 4) {
            if (m_loop) {
                if (!qEnvironmentVariableIsEmpty("SC_DEBUG"))
                    std::fprintf(stderr, "[loop] pos=%lld out=%lld -> in=%lld\n", pos, outMs, inMs);
                m_seekGuard = true;
                m_player->setPosition(inMs);
                m_playheadMs = double(inMs);
                QTimer::singleShot(90, this, [this] { m_seekGuard = false; });
            } else {
                m_wantPlay = false;
                m_player->pause();
                const qint64 rest = qMax(inMs, outMs - 20);
                m_player->setPosition(rest);
                m_playheadMs = double(rest);
            }
        }
    }

    const double pos = double(m_player->position());
    if (!qFuzzyCompare(pos + 1.0, m_playheadMs + 1.0)) {
        m_playheadMs = pos;
        emit playbackChanged();
    }
}

double Backend::playheadFraction() const
{
    const ClipData *c = current();
    if (!c)
        return 0.0;
    const double duration = m_viewingRender ? m_player->duration() / 1000.0 : c->meta.duration;
    if (duration <= 0)
        return 0.0;
    return clampd(m_playheadMs / 1000.0 / duration, 0.0, 1.0);
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */
/* ------------------------------------------------------------------ */

QString Backend::uniqueOutputPath(const QString &baseName) const
{
    const QString clean = sanitizeName(baseName);
    const QString suffix = m_settings.suffix.isEmpty() ? QStringLiteral("-edit") : m_settings.suffix;
    const QString ext = outputExtension(m_settings.format);
    const QDir dir(m_settings.outputDir);

    for (int n = 1; n <= 1000; ++n) {
        const QString candidate = dir.filePath(clean + suffix
                                               + (n == 1 ? QString() : QStringLiteral("-%1").arg(n))
                                               + QStringLiteral(".") + ext);
        if (m_queue->reserves(candidate))
            continue;
        bool ours = false;
        for (const Edit &e : m_edits) {
            if (e.outputPath && *e.outputPath == candidate) {
                ours = true;
                break;
            }
        }
        if (ours)
            return candidate;
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return dir.filePath(clean + suffix + QStringLiteral("-")
                        + QString::number(qint64(nowMs())) + QStringLiteral(".") + ext);
}

void Backend::enqueueRender(const QStringList &paths)
{
    if (!m_ffmpegOk) {
        emit toast(QStringLiteral("ffmpeg was not found on this machine"), QStringLiteral("error"));
        return;
    }

    QStringList seen;
    for (const QString &path : paths) {
        if (seen.contains(path))
            continue;
        seen << path;

        const ClipData *c = m_model->find(path);
        if (!c || c->loading || !c->error.isEmpty())
            continue;

        QString outPath;
        const QString ext = outputExtension(m_settings.format);
        // Reuse our own output only when it has the current container's
        // extension; otherwise a format switch would write ProRes into a .mp4.
        if (c->edit.outputPath
            && QFileInfo(*c->edit.outputPath).suffix().compare(ext, Qt::CaseInsensitive) == 0
            && QDir::cleanPath(QFileInfo(*c->edit.outputPath).absolutePath())
                == QDir::cleanPath(m_settings.outputDir)) {
            outPath = *c->edit.outputPath;
        } else {
            outPath = uniqueOutputPath(QFileInfo(path).completeBaseName());
        }
        if (m_queue->reserves(outPath))
            continue;

        Job job;
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.path = path;
        job.outPath = outPath;
        job.edit = c->edit;
        job.hasAudio = c->meta.hasAudio;
        job.audioCodec = c->meta.audioCodec;
        job.keepAudio = c->edit.audio.value_or(m_settings.audio);
        job.duration = qMax(0.05, c->edit.out - c->edit.in);
        job.format = m_settings.format;
        job.crf = m_settings.crf;
        job.preset = m_settings.preset;
        job.createdAt = qint64(nowMs());
        m_queue->enqueue(job);
    }
    emit queueChanged();
}

void Backend::renderCurrent()
{
    flushSave();
    if (m_currentPath.isEmpty())
        return;
    enqueueRender({m_currentPath});
}

void Backend::renderEdited()
{
    flushSave();
    QStringList paths;
    for (const ClipData &c : m_model->editedClips())
        paths << c.path;
    enqueueRender(paths);
}

void Backend::renderAll()
{
    flushSave();
    QStringList paths;
    for (const ClipData &c : m_model->all()) {
        if (c.error.isEmpty() && !c.loading)
            paths << c.path;
    }
    enqueueRender(paths);
}

void Backend::cancelJob(const QString &id)
{
    m_queue->cancel(id);
}

void Backend::retryJob(const QString &path)
{
    enqueueRender({path});
}

void Backend::persistJobResult(const Job &job)
{
    Edit e = job.edit;
    e.outputPath = job.outPath;
    e.renderedAt = nowMs();
    if (!e.modifiedAt)
        e.modifiedAt = job.edit.modifiedAt.value_or(nowMs());
    m_edits.insert(job.path, e);
    saveEdits();
}

int Backend::activeJobs() const
{
    return m_queue->activeCount();
}

QString Backend::queueSummary() const
{
    int running = 0, queued = 0, done = 0, failed = 0;
    for (int i = 0; i < m_queue->rowCount(); ++i) {
        const QString st = m_queue->data(m_queue->index(i, 0), RenderQueue::StatusRole).toString();
        if (st == QLatin1String("running"))
            ++running;
        else if (st == QLatin1String("queued"))
            ++queued;
        else if (st == QLatin1String("done"))
            ++done;
        else if (st == QLatin1String("error"))
            ++failed;
    }
    if (running > 0)
        return QStringLiteral("rendering · %1 left").arg(running + queued);
    if (queued > 0)
        return QStringLiteral("%1 queued").arg(queued);
    if (failed > 0 && done == 0)
        return QStringLiteral("%1 failed").arg(failed);
    if (done > 0)
        return QStringLiteral("%1 rendered").arg(done);
    return {};
}

/* ------------------------------------------------------------------ */
/* settings + misc                                                     */
/* ------------------------------------------------------------------ */

void Backend::saveSettings(QVariantMap values)
{
    const QJsonObject patch = QJsonObject::fromVariantMap(values);
    const QString previousRoot = m_settings.root;
    m_settings.applyPatch(patch);
    QDir().mkpath(m_settings.root);
    QDir().mkpath(m_settings.outputDir);
    writeJson(m_paths.settingsFile(), m_settings.toJson());

    emit settingsChanged();
    emit toast(QStringLiteral("Settings saved"), QStringLiteral("ok"));

    if (previousRoot != m_settings.root) {
        m_currentPath.clear();
        m_model->setActive({});
        m_stripUrl.clear();
        m_stripWant.clear();
        emit stripChanged();
        emit currentChanged();
    }
    rescan();
}

QUrl Backend::pathToUrl(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(trimmed).absoluteFilePath());
}

QString Backend::pathFromUrl(const QUrl &url) const
{
    return url.toLocalFile();
}

void Backend::revealOutputs()
{
    QDir().mkpath(m_settings.outputDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.outputDir));
}

void Backend::revealPath(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath()
                                                             : fi.absolutePath()));
}

QString Backend::fmt(double seconds, int decimals) const
{
    return fmtTime(seconds, decimals);
}

double Backend::parseTc(const QString &text) const
{
    return parseTime(text);
}

/* ------------------------------------------------------------------ */
/* current-clip getters                                                */
/* ------------------------------------------------------------------ */

int Backend::clipCount() const { return m_model->rowCount(); }

int Backend::editedCount() const
{
    int n = 0;
    for (const ClipData &c : m_model->all()) {
        if (c.edited && c.error.isEmpty())
            ++n;
    }
    return n;
}

QString Backend::curName() const
{
    const ClipData *c = current();
    return c ? c->name : QString();
}

QString Backend::curRel() const
{
    const ClipData *c = current();
    return c ? c->rel : QString();
}

QString Backend::curError() const
{
    const ClipData *c = current();
    return c ? c->error : QString();
}

QString Backend::hudMeta() const
{
    const ClipData *c = current();
    if (!c)
        return {};
    QStringList parts;
    parts << QStringLiteral("%1×%2").arg(c->meta.displayWidth).arg(c->meta.displayHeight);
    if (c->meta.fps > 0)
        parts << QStringLiteral("%1 fps").arg(c->meta.fps, 0, 'f', 2);
    parts << fmtTime(c->meta.duration);
    if (c->meta.rotation)
        parts << QStringLiteral("file rotation %1°").arg(c->meta.rotation);
    if (c->meta.hasAudio)
        parts << (c->meta.audioCodec.isEmpty() ? QStringLiteral("audio")
                                              : c->meta.audioCodec);
    else
        parts << QStringLiteral("silent");
    return parts.join(QStringLiteral("  ·  "));
}

double Backend::curDuration() const
{
    const ClipData *c = current();
    return c ? c->meta.duration : 0.0;
}

double Backend::curFps() const
{
    const ClipData *c = current();
    return c ? c->meta.fps : 0.0;
}

int Backend::curDisplayW() const
{
    const ClipData *c = current();
    return c ? c->meta.displayWidth : 0;
}

int Backend::curDisplayH() const
{
    const ClipData *c = current();
    return c ? c->meta.displayHeight : 0;
}

bool Backend::curHasAudio() const
{
    const ClipData *c = current();
    return c ? c->meta.hasAudio : false;
}

bool Backend::curRendered() const
{
    const ClipData *c = current();
    return c ? c->rendered : false;
}

bool Backend::curStale() const
{
    const ClipData *c = current();
    return c ? c->stale : false;
}

double Backend::editIn() const
{
    const ClipData *c = current();
    return c ? c->edit.in : 0.0;
}

double Backend::editOut() const
{
    const ClipData *c = current();
    return c ? c->edit.out : 0.0;
}

double Backend::editLength() const
{
    const ClipData *c = current();
    return c ? qMax(0.0, c->edit.out - c->edit.in) : 0.0;
}

int Backend::rotate() const
{
    const ClipData *c = current();
    return c ? c->edit.rotate : 0;
}

bool Backend::flipH() const
{
    const ClipData *c = current();
    return c ? c->edit.flipH : false;
}

bool Backend::flipV() const
{
    const ClipData *c = current();
    return c ? c->edit.flipV : false;
}

bool Backend::keepAudio() const
{
    const ClipData *c = current();
    if (!c)
        return false;
    return c->edit.audio.value_or(m_settings.audio);
}

bool Backend::shortWarn() const
{
    const ClipData *c = current();
    return c && m_settings.minDuration > 0 && editLength() < m_settings.minDuration;
}

QString Backend::shortWarnText() const
{
    if (!shortWarn())
        return {};
    return QStringLiteral("Shorter than the %1 s minimum").arg(m_settings.minDuration, 0, 'g', 3);
}

QString Backend::editInText() const { return fmtTime(editIn()); }
QString Backend::editOutText() const { return fmtTime(editOut()); }
QString Backend::editLenText() const { return fmtTime(editLength()); }

} // namespace sc
