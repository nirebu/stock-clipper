// Headless driver for verification: scans the library, applies a trim +
// rotation to one clip, renders it and reports the result. Not part of the
// app; build it with the stock-clipper-selftest target.

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>

#include <cstdio>

#include "backend.h"

using namespace sc;

static void say(const QString &line)
{
    std::fprintf(stderr, "%s\n", qPrintable(line));
    std::fflush(stderr);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("nirebu"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("nirebu.local"));
    QCoreApplication::setApplicationName(QStringLiteral("stock-clipper"));

    // SC_FORMAT=h264|prores|dnxhr, seeded before the backend loads settings.
    const QString format = qEnvironmentVariable("SC_FORMAT");
    if (!format.isEmpty()) {
        const Paths paths = Paths::discover();
        QJsonObject settings = readJson(paths.settingsFile());
        settings.insert(QStringLiteral("format"), format);
        // The file now exists, so Backend skips its first-run ./input default.
        if (!settings.contains("root") && QFileInfo(QStringLiteral("input")).isDir()) {
            settings.insert("root", QDir::current().filePath("input"));
            settings.insert("outputDir", QDir::current().filePath("renders"));
        }
        writeJson(paths.settingsFile(), settings);
    }

    Backend backend;
    const QString wanted = qEnvironmentVariable("SC_CLIP");
    const bool doRender = qEnvironmentVariable("SC_NO_RENDER").isEmpty();

    bool renderStarted = false;
    bool done = false;

    QTimer poll;
    poll.setInterval(250);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (!renderStarted || done)
            return;
        if (backend.activeJobs() > 0)
            return;

        auto *jobs = backend.jobs();
        done = true;
        poll.stop();
        if (jobs->rowCount() == 0) {
            say(QStringLiteral("FAIL: no job was queued"));
            app.exit(3);
            return;
        }
        const auto idx = jobs->index(0, 0);
        const QString status = jobs->data(idx, RenderQueue::StatusRole).toString();
        const QString outPath = jobs->data(idx, RenderQueue::OutPathRole).toString();
        say(QStringLiteral("job status: %1").arg(status));
        say(QStringLiteral("output: %1").arg(outPath));
        say(QStringLiteral("error: %1").arg(jobs->data(idx, RenderQueue::ErrorRole).toString()));
        app.exit(status == QLatin1String("done") ? 0 : 4);
    });

    QTimer::singleShot(2500, [&] {
        auto *clips = backend.clips();
        say(QStringLiteral("clips: %1  edited: %2  jobs: %3")
                .arg(clips->rowCount())
                .arg(backend.editedCount())
                .arg(backend.jobs()->rowCount()));

        QString target;
        for (int i = 0; i < clips->rowCount(); ++i) {
            const auto idx = clips->index(i, 0);
            const QString path = clips->data(idx, ClipModel::PathRole).toString();
            const QString error = clips->data(idx, ClipModel::ErrorRole).toString();
            if (i < 6) {
                say(QStringLiteral("  %1  %2x%3  %4s  err=%5")
                        .arg(QFileInfo(path).fileName())
                        .arg(clips->data(idx, ClipModel::DisplayWidthRole).toInt())
                        .arg(clips->data(idx, ClipModel::DisplayHeightRole).toInt())
                        .arg(clips->data(idx, ClipModel::DurationRole).toDouble(), 0, 'f', 2)
                        .arg(error));
            }
            if (target.isEmpty() && error.isEmpty()
                && (wanted.isEmpty() || path.contains(wanted))) {
                target = path;
            }
        }

        if (target.isEmpty()) {
            say(QStringLiteral("FAIL: no readable clip found (root=%1)").arg(backend.root()));
            app.exit(2);
            return;
        }

        say(QStringLiteral("selected: %1").arg(target));
        backend.selectClip(target);
        say(QStringLiteral("before: in=%1 out=%2 rotate=%3 len=%4")
                .arg(backend.editIn(), 0, 'f', 3)
                .arg(backend.editOut(), 0, 'f', 3)
                .arg(backend.rotate())
                .arg(backend.editLength(), 0, 'f', 3));

        backend.setIn(0.5);
        backend.setOut(2.0);
        backend.setRotate(90);
        backend.flushSave();

        say(QStringLiteral("after:  in=%1 out=%2 rotate=%3 len=%4")
                .arg(backend.editIn(), 0, 'f', 3)
                .arg(backend.editOut(), 0, 'f', 3)
                .arg(backend.rotate())
                .arg(backend.editLength(), 0, 'f', 3));

        if (!doRender) {
            app.exit(0);
            return;
        }

        backend.renderCurrent();
        renderStarted = true;
        poll.start();
        say(QStringLiteral("rendering…"));
    });

    QTimer::singleShot(120000, [&] {
        say(QStringLiteral("FAIL: timed out"));
        app.exit(5);
    });

    return app.exec();
}
