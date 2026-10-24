#include "render.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QUuid>
#include <QtMath>

#include "media.h"

namespace sc {

RenderQueue::RenderQueue(QObject *parent) : QAbstractListModel(parent) {}

int RenderQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_jobs.size();
}

int RenderQueue::indexOf(const QString &id) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id)
            return i;
    }
    return -1;
}

Job *RenderQueue::job(const QString &id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_jobs[i];
}

bool RenderQueue::active() const
{
    for (const Job &j : m_jobs) {
        if (j.status == QLatin1String("running") || j.status == QLatin1String("queued"))
            return true;
    }
    return false;
}

int RenderQueue::activeCount() const
{
    int n = 0;
    for (const Job &j : m_jobs) {
        if (j.status == QLatin1String("running") || j.status == QLatin1String("queued"))
            ++n;
    }
    return n;
}

bool RenderQueue::reserves(const QString &outPath) const
{
    for (const Job &j : m_jobs) {
        if (j.outPath == outPath
            && (j.status == QLatin1String("running") || j.status == QLatin1String("queued")))
            return true;
    }
    return false;
}

QVariant RenderQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};
    const Job &j = m_jobs.at(index.row());

    switch (role) {
    case IdRole:
        return j.id;
    case PathRole:
        return j.path;
    case OutPathRole:
        return j.outPath;
    case OutNameRole:
        return QFileInfo(j.outPath).fileName();
    case NameRole:
        return QFileInfo(j.path).fileName();
    case StatusRole:
        return j.status;
    case ProgressRole:
        return j.progress;
    case PercentRole:
        return int(qRound(j.progress * 100));
    case SpeedRole:
        return j.speed;
    case MetaTextRole: {
        if (j.status == QLatin1String("running")) {
            QString text = QStringLiteral("%1%").arg(int(qRound(j.progress * 100)));
            if (!j.speed.isEmpty())
                text += QStringLiteral(" · ") + j.speed;
            return text;
        }
        if (j.status == QLatin1String("queued"))
            return QStringLiteral("waiting");
        if (j.status == QLatin1String("done"))
            return fmtTime(j.duration);
        if (j.status == QLatin1String("error"))
            return QStringLiteral("failed");
        return QStringLiteral("canceled");
    }
    case ErrorRole:
        return j.error;
    case CanPlayRole:
        return j.status == QLatin1String("done");
    case CanCancelRole:
        return j.status == QLatin1String("running") || j.status == QLatin1String("queued");
    case CanRetryRole:
        return j.status == QLatin1String("error");
    case DurationRole:
        return j.duration;
    default:
        return {};
    }
}

QHash<int, QByteArray> RenderQueue::roleNames() const
{
    return {
        {IdRole, "jobId"},
        {PathRole, "jobPath"},
        {OutPathRole, "outPath"},
        {OutNameRole, "outName"},
        {NameRole, "jobName"},
        {StatusRole, "status"},
        {ProgressRole, "progress"},
        {PercentRole, "percent"},
        {SpeedRole, "speed"},
        {MetaTextRole, "metaText"},
        {ErrorRole, "jobError"},
        {CanPlayRole, "canPlay"},
        {CanCancelRole, "canCancel"},
        {CanRetryRole, "canRetry"},
        {DurationRole, "jobDuration"},
    };
}

void RenderQueue::enqueue(const Job &job)
{
    beginInsertRows({}, 0, 0);
    m_jobs.prepend(job);
    endInsertRows();
    m_pending.append(job.id);
    emit activeChanged();
    pump();
}

void RenderQueue::cancel(const QString &id)
{
    Job *j = job(id);
    if (!j)
        return;

    if (j->status == QLatin1String("queued")) {
        m_pending.removeAll(id);
        j->status = QStringLiteral("canceled");
        j->endedAt = qint64(nowMs());
        const QModelIndex idx = index(indexOf(id), 0);
        emit dataChanged(idx, idx);
        emit activeChanged();
        return;
    }

    if (j->status == QLatin1String("running") && m_proc) {
        m_canceled = true;
        m_proc->kill();
    }
}

void RenderQueue::pump()
{
    if (m_proc || m_pending.isEmpty())
        return;

    m_currentId = m_pending.takeFirst();
    Job *j = job(m_currentId);
    if (!j) {
        pump();
        return;
    }

    if (j->status != QLatin1String("queued")) { // canceled while waiting
        pump();
        return;
    }

    j->status = QStringLiteral("running");
    j->progress = 0.0;
    j->speed.clear();
    j->error.clear();
    {
        const QModelIndex idx = index(indexOf(m_currentId), 0);
        emit dataChanged(idx, idx);
    }
    emit activeChanged();

    RenderSpec spec;
    spec.input = j->path;
    spec.output = j->outPath;
    spec.in = j->edit.in;
    spec.duration = j->duration;
    spec.rotate = j->edit.rotate;
    spec.flipH = j->edit.flipH;
    spec.flipV = j->edit.flipV;
    spec.hasAudio = j->hasAudio;
    spec.audioCodec = j->audioCodec;
    spec.keepAudio = j->keepAudio;
    spec.crf = j->crf;
    spec.preset = j->preset;

    m_stdoutBuf.clear();
    m_stderr.clear();
    m_canceled = false;

    m_proc = new QProcess(this);
    m_proc->setProgram(QStringLiteral("ffmpeg"));
    m_proc->setArguments(renderArgs(spec));
    m_proc->setStandardInputFile(QProcess::nullDevice());

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdoutBuf += m_proc->readAllStandardOutput();
        int nl = 0;
        while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
            const QByteArray line = m_stdoutBuf.left(nl).trimmed();
            m_stdoutBuf.remove(0, nl + 1);
            handleLine(line);
        }
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_proc->readAllStandardError();
        if (m_stderr.size() > 12000)
            m_stderr = m_stderr.right(12000);
    });
    connect(m_proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
                m_stderr += m_proc->readAllStandardError();
                const QString id = m_currentId;
                m_proc->deleteLater();
                m_proc = nullptr;

                Job *cur = job(id);
                if (!cur) {
                    m_currentId.clear();
                    pump();
                    return;
                }

                const QString outPath = cur->outPath;
                if (m_canceled) {
                    QFile::remove(outPath);
                    finishCurrent(QStringLiteral("canceled"), {});
                } else if (code == 0 && status == QProcess::NormalExit
                           && QFileInfo::exists(outPath)) {
                    finishCurrent(QStringLiteral("done"), {});
                } else {
                    QString message = QString::fromUtf8(m_stderr).trimmed();
                    if (message.isEmpty())
                        message = QStringLiteral("ffmpeg exited with code %1").arg(code);
                    QFile::remove(outPath);
                    finishCurrent(QStringLiteral("error"), message);
                }
                m_currentId.clear();
                pump();
            });

    m_proc->start();
}

void RenderQueue::handleLine(const QByteArray &line)
{
    const int eq = line.indexOf('=');
    if (eq <= 0)
        return;
    const QByteArray key = line.left(eq).trimmed();
    const QByteArray value = line.mid(eq + 1).trimmed();

    Job *j = job(m_currentId);
    if (!j)
        return;

    // out_time_us / out_time_ms are both microseconds despite the name
    if (key == "out_time_us" || key == "out_time_ms") {
        bool ok = false;
        const double micros = value.toDouble(&ok);
        if (ok && j->duration > 0) {
            const double p = clampd(micros / 1e6 / j->duration, 0.0, 1.0);
            if (p > j->progress) {
                j->progress = p;
                const QModelIndex idx = index(indexOf(m_currentId), 0);
                emit dataChanged(idx, idx, {ProgressRole, PercentRole, MetaTextRole});
            }
        }
    } else if (key == "speed") {
        j->speed = QString::fromUtf8(value);
        const QModelIndex idx = index(indexOf(m_currentId), 0);
        emit dataChanged(idx, idx, {SpeedRole, MetaTextRole});
    } else if (key == "progress" && value == "end") {
        j->progress = 1.0;
        const QModelIndex idx = index(indexOf(m_currentId), 0);
        emit dataChanged(idx, idx, {ProgressRole, PercentRole, MetaTextRole});
    }
}

void RenderQueue::finishCurrent(const QString &status, const QString &error)
{
    Job *j = job(m_currentId);
    if (!j)
        return;
    j->status = status;
    j->error = error;
    j->endedAt = qint64(nowMs());
    if (status == QLatin1String("done"))
        j->progress = 1.0;

    const QModelIndex idx = index(indexOf(m_currentId), 0);
    emit dataChanged(idx, idx);
    emit activeChanged();
    emit jobFinished(*j, status == QLatin1String("done"));
}

} // namespace sc
