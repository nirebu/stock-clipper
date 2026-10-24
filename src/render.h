#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

#include "core.h"

namespace sc {

struct Job {
    QString id;
    QString path;
    QString outPath;
    Edit edit;
    bool hasAudio = false;
    QString audioCodec;
    bool keepAudio = false;
    QString format = QStringLiteral("h264");
    int crf = 18;
    QString preset = QStringLiteral("medium");
    double duration = 0.0;
    double progress = 0.0;
    QString speed;
    QString status = QStringLiteral("queued");
    QString error;
    qint64 createdAt = 0;
    qint64 endedAt = 0;
};

/// In-memory, strictly serial: x264 already uses every core.
class RenderQueue : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        PathRole,
        OutPathRole,
        OutNameRole,
        NameRole,
        StatusRole,
        ProgressRole,
        PercentRole,
        SpeedRole,
        MetaTextRole,
        ErrorRole,
        CanPlayRole,
        CanCancelRole,
        CanRetryRole,
        DurationRole,
    };

    explicit RenderQueue(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void enqueue(const Job &job);
    void cancel(const QString &id);

    bool active() const;
    bool reserves(const QString &outPath) const;
    int activeCount() const;

signals:
    void jobFinished(const sc::Job &job, bool ok);
    void activeChanged();

private:
    QVector<Job> m_jobs;
    QStringList m_pending;
    QProcess *m_proc = nullptr;
    QString m_currentId;
    QByteArray m_stdoutBuf;
    QByteArray m_stderr;
    bool m_canceled = false;

    Job *job(const QString &id);
    int indexOf(const QString &id) const;
    void pump();
    void handleLine(const QByteArray &line);
    void finishCurrent(const QString &status, const QString &error);
};

} // namespace sc
