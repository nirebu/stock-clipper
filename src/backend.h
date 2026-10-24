#pragma once

#include <QHash>
#include <QMediaPlayer>
#include <functional>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "core.h"
#include "library.h"
#include "render.h"

class QAudioOutput;

namespace sc {

class Backend : public QObject
{
    Q_OBJECT

    // models
    Q_PROPERTY(sc::ClipModel *clips READ clips CONSTANT)
    Q_PROPERTY(sc::ClipFilterModel *filteredClips READ filteredClips CONSTANT)
    Q_PROPERTY(sc::RenderQueue *jobs READ jobs CONSTANT)
    Q_PROPERTY(QObject *player READ player CONSTANT)

    // settings
    Q_PROPERTY(QString root READ root NOTIFY settingsChanged)
    Q_PROPERTY(QString outputDir READ outputDir NOTIFY settingsChanged)
    Q_PROPERTY(QString suffix READ suffix NOTIFY settingsChanged)
    Q_PROPERTY(int crf READ crf NOTIFY settingsChanged)
    Q_PROPERTY(QString preset READ preset NOTIFY settingsChanged)
    Q_PROPERTY(double minDuration READ minDuration NOTIFY settingsChanged)
    Q_PROPERTY(bool audioDefault READ audioDefault NOTIFY settingsChanged)
    Q_PROPERTY(bool ffmpegOk READ ffmpegOk NOTIFY settingsChanged)

    // library
    Q_PROPERTY(int clipCount READ clipCount NOTIFY libraryChanged)
    Q_PROPERTY(int editedCount READ editedCount NOTIFY libraryChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY libraryChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY libraryChanged)

    // selection
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY currentChanged)
    Q_PROPERTY(QString curName READ curName NOTIFY currentChanged)
    Q_PROPERTY(QString curRel READ curRel NOTIFY currentChanged)
    Q_PROPERTY(QString curError READ curError NOTIFY currentChanged)
    Q_PROPERTY(QString hudMeta READ hudMeta NOTIFY currentChanged)
    Q_PROPERTY(double curDuration READ curDuration NOTIFY currentChanged)
    Q_PROPERTY(double curFps READ curFps NOTIFY currentChanged)
    Q_PROPERTY(int curDisplayW READ curDisplayW NOTIFY currentChanged)
    Q_PROPERTY(int curDisplayH READ curDisplayH NOTIFY currentChanged)
    Q_PROPERTY(bool curHasAudio READ curHasAudio NOTIFY currentChanged)
    Q_PROPERTY(bool curRendered READ curRendered NOTIFY currentChanged)
    Q_PROPERTY(bool curStale READ curStale NOTIFY currentChanged)
    Q_PROPERTY(QString stripUrl READ stripUrl NOTIFY stripChanged)
    Q_PROPERTY(bool stripLoading READ stripLoading NOTIFY stripChanged)

    // edit
    Q_PROPERTY(double editIn READ editIn NOTIFY editChanged)
    Q_PROPERTY(double editOut READ editOut NOTIFY editChanged)
    Q_PROPERTY(double editLength READ editLength NOTIFY editChanged)
    Q_PROPERTY(int rotate READ rotate NOTIFY editChanged)
    Q_PROPERTY(bool flipH READ flipH NOTIFY editChanged)
    Q_PROPERTY(bool flipV READ flipV NOTIFY editChanged)
    Q_PROPERTY(bool keepAudio READ keepAudio NOTIFY editChanged)
    Q_PROPERTY(bool shortWarn READ shortWarn NOTIFY editChanged)
    Q_PROPERTY(QString shortWarnText READ shortWarnText NOTIFY editChanged)
    Q_PROPERTY(QString editInText READ editInText NOTIFY editChanged)
    Q_PROPERTY(QString editOutText READ editOutText NOTIFY editChanged)
    Q_PROPERTY(QString editLenText READ editLenText NOTIFY editChanged)

    // playback
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool viewingRender READ viewingRender NOTIFY currentChanged)
    Q_PROPERTY(double playheadFraction READ playheadFraction NOTIFY playbackChanged)
    Q_PROPERTY(QString renderedName READ renderedName NOTIFY currentChanged)

    // queue
    Q_PROPERTY(int activeJobs READ activeJobs NOTIFY queueChanged)
    Q_PROPERTY(QString queueSummary READ queueSummary NOTIFY queueChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    ClipModel *clips() const { return m_model; }
    ClipFilterModel *filteredClips() const { return m_filter; }
    RenderQueue *jobs() const { return m_queue; }
    QObject *player() const { return static_cast<QObject *>(m_player); }

    QString root() const { return m_settings.root; }
    QString outputDir() const { return m_settings.outputDir; }
    QString suffix() const { return m_settings.suffix; }
    int crf() const { return m_settings.crf; }
    QString preset() const { return m_settings.preset; }
    double minDuration() const { return m_settings.minDuration; }
    bool audioDefault() const { return m_settings.audio; }
    bool ffmpegOk() const { return m_ffmpegOk; }

    int clipCount() const;
    int editedCount() const;
    bool scanning() const { return m_scanning; }
    QString filter() const;

    bool hasSelection() const { return !m_currentPath.isEmpty(); }
    QString curName() const;
    QString curRel() const;
    QString curError() const;
    QString hudMeta() const;
    double curDuration() const;
    double curFps() const;
    int curDisplayW() const;
    int curDisplayH() const;
    bool curHasAudio() const;
    bool curRendered() const;
    bool curStale() const;
    QString stripUrl() const { return m_stripUrl; }
    bool stripLoading() const { return m_stripLoading; }

    double editIn() const;
    double editOut() const;
    double editLength() const;
    int rotate() const;
    bool flipH() const;
    bool flipV() const;
    bool keepAudio() const;
    bool shortWarn() const;
    QString shortWarnText() const;
    QString editInText() const;
    QString editOutText() const;
    QString editLenText() const;

    bool playing() const { return m_playing; }
    bool viewingRender() const { return m_viewingRender; }
    double playheadFraction() const;
    QString renderedName() const { return m_renderedName; }

    int activeJobs() const;
    QString queueSummary() const;

    /* commands */
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void selectClip(const QString &path);
    Q_INVOKABLE void selectSourceRow(int sourceRow);
    Q_INVOKABLE void setFilter(const QString &f);

    Q_INVOKABLE void setIn(double t);
    Q_INVOKABLE void setOut(double t);
    Q_INVOKABLE void nudgeIn(double delta);
    Q_INVOKABLE void nudgeOut(double delta);
    Q_INVOKABLE void setRotate(int r);
    Q_INVOKABLE void toggleFlipH();
    Q_INVOKABLE void toggleFlipV();
    Q_INVOKABLE void rotateRow(const QString &path);
    Q_INVOKABLE void setAudioChoice(bool on);
    Q_INVOKABLE void resetEdit();
    Q_INVOKABLE void commitFieldText(const QString &which, const QString &text);

    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void attachVideoSink(QObject *sink);
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void stepFrame(int dir, bool big = false);
    Q_INVOKABLE void setInAtPlayhead();
    Q_INVOKABLE void setOutAtPlayhead();
    Q_INVOKABLE void setLoop(bool on);
    Q_INVOKABLE void playRendered(const QString &outPath);
    Q_INVOKABLE void backToSource();

    Q_INVOKABLE void renderCurrent();
    Q_INVOKABLE void renderEdited();
    Q_INVOKABLE void renderAll();
    Q_INVOKABLE void cancelJob(const QString &id);
    Q_INVOKABLE void retryJob(const QString &path);

    Q_INVOKABLE void saveSettings(QVariantMap values);
    Q_INVOKABLE QUrl pathToUrl(const QString &path) const;
    Q_INVOKABLE QString pathFromUrl(const QUrl &url) const;
    Q_INVOKABLE void revealOutputs();
    Q_INVOKABLE void revealPath(const QString &path);

    Q_INVOKABLE QString fmt(double seconds, int decimals = 2) const;
    Q_INVOKABLE double parseTc(const QString &text) const;
    Q_INVOKABLE void flushSave();

signals:
    void settingsChanged();
    void libraryChanged();
    void currentChanged();
    void editChanged();
    void playbackChanged();
    void stripChanged();
    void queueChanged();
    void toast(const QString &message, const QString &kind);

private:
    struct ProbeEntry {
        double mtimeMs = 0;
        qint64 size = 0;
        Meta meta;
    };

    Paths m_paths;
    Settings m_settings;
    QHash<QString, Edit> m_edits;
    QHash<QString, ProbeEntry> m_probe;

    ClipModel *m_model = nullptr;
    ClipFilterModel *m_filter = nullptr;
    RenderQueue *m_queue = nullptr;
    Prober *m_prober = nullptr;
    Thumbnailer *m_thumbs = nullptr;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;

    QTimer m_tick;
    QTimer m_save;
    QTimer m_stripTimer;

    QString m_currentPath;
    QString m_renderedName;
    bool m_viewingRender = false;
    bool m_playing = false;
    bool m_loop = true;
    bool m_scanning = false;
    bool m_ffmpegOk = false;
    bool m_seekGuard = false;
    double m_playheadMs = 0;
    QString m_stripUrl;
    QString m_stripWant;
    bool m_stripLoading = false;
    bool m_priming = false;
    bool m_wantPlay = false;
    double m_pendingSeekMs = -1.0;
    bool m_dirty = false;

    void tick();
    void primeFrame();
    void loadState();
    void saveEdits();
    void saveProbeCache();
    void decorate(ClipData &clip, bool requestPreview);
    void applyEditTo(const QString &path, const std::function<void(Edit &)> &mutate);
    void applyEdit(const std::function<void(Edit &)> &mutate, const QString &seek = {});
    void syncCurrent();
    void loadStrip();
    void updateThumb(const QString &path);
    void requestProbe(const QString &path);
    void refreshAllRows();
    void enqueueRender(const QStringList &paths);
    QString uniqueOutputPath(const QString &base) const;
    void persistJobResult(const Job &job);
    const ClipData *current() const;
};

} // namespace sc
