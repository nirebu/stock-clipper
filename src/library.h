#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <functional>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>

#include "core.h"

namespace sc {

/// Depth-3 walk, hidden entries and non-video files skipped.
QStringList listVideos(const QString &root);

/* ------------------------------------------------------------------ */
/* ffprobe, at most 4 in flight                                        */
/* ------------------------------------------------------------------ */

class Prober : public QObject
{
    Q_OBJECT
public:
    explicit Prober(QObject *parent = nullptr);
    void request(const QString &path);

signals:
    void probed(const QString &path, const sc::Meta &meta, const QString &error);

private:
    struct Job {
        QProcess *proc = nullptr;
        QString path;
        QByteArray out;
    };
    QList<Job *> m_active;
    QStringList m_pending;
    QSet<QString> m_inflight;

    void pump();
};

/* ------------------------------------------------------------------ */
/* thumbnails + filmstrips                                             */
/* ------------------------------------------------------------------ */

/// Generated *with* the clip's orientation and keyed by it, so rotating a
/// clip cannot keep showing sideways frames.
class Thumbnailer : public QObject
{
    Q_OBJECT
public:
    explicit Thumbnailer(const QString &cacheDir, QObject *parent = nullptr);

    /// Cached file for this exact (path, mtime, size, kind, orientation), or "".
    QString cached(const QString &path, const QString &kind, double duration, int rotate,
                   bool flipH, bool flipV, double mtimeMs, qint64 size) const;

    /// Cache file this request would produce (used to ignore stale results).
    QString pathFor(const QString &path, const QString &kind, double duration, int rotate,
                    bool flipH, bool flipV, double mtimeMs, qint64 size) const;

    /// Generate if needed. Emits ready() when the file exists.
    void request(const QString &path, const QString &kind, double duration, int rotate,
                 bool flipH, bool flipV, double mtimeMs, qint64 size);

signals:
    void ready(const QString &kind, const QString &path, const QString &file);
    void failed(const QString &kind, const QString &path);

private:
    struct Req {
        QString path;
        QString kind;
        QString out;
        double duration = 0;
        int rotate = 0;
        bool flipH = false;
        bool flipV = false;
    };

    QString m_cacheDir;
    QList<Req> m_pending;
    QSet<QString> m_inflight;
    int m_active = 0;

    QString outFor(const QString &path, const QString &kind, int rotate, bool flipH, bool flipV,
                   double mtimeMs, qint64 size) const;
    void pump();
};

/* ------------------------------------------------------------------ */
/* clip list                                                           */
/* ------------------------------------------------------------------ */

class ClipModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        NameRole,
        RelRole,
        DurationRole,
        DisplayWidthRole,
        DisplayHeightRole,
        FpsRole,
        ThumbRole,
        FlagsRole,
        ErrorRole,
        EditedRole,
        RenderedRole,
        StaleRole,
        RotateRole,
        FlipHRole,
        FlipVRole,
        HasAudioRole,
        ActiveRole,
        LoadingRole,
    };

    explicit ClipModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setClips(QVector<ClipData> clips);
    void upsert(const ClipData &clip);
    void setActive(const QString &path);

    int rowOf(const QString &path) const;
    const ClipData *find(const QString &path) const;
    QVector<ClipData> all() const { return m_clips; }
    QVector<ClipData> editedClips() const;
    QString activePath() const { return m_active; }

    /// Mutate one clip and refresh its row.
    void update(const QString &path, const std::function<void(ClipData &)> &fn);

private:
    QVector<ClipData> m_clips;
    QString m_active;

    void emitRow(int row);
    QVariantList flagsFor(const ClipData &clip) const;
};

class ClipFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
public:
    explicit ClipFilterModel(QObject *parent = nullptr);

    QString filter() const { return m_filter; }
    void setFilter(const QString &f);

    Q_INVOKABLE int sourceRow(int proxyRow) const;

signals:
    void filterChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filter;
};

} // namespace sc
