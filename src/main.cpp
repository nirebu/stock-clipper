#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>

#include "backend.h"

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *tag = "info";
    switch (type) {
    case QtDebugMsg:
        tag = "debug";
        break;
    case QtInfoMsg:
        tag = "info";
        break;
    case QtWarningMsg:
        tag = "warn";
        break;
    case QtCriticalMsg:
        tag = "error";
        break;
    case QtFatalMsg:
        tag = "fatal";
        break;
    }
    std::fprintf(stderr, "[%s] %s\n", tag, qPrintable(msg));
    std::fflush(stderr);
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(messageHandler);
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("nirebu"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("nirebu.local"));
    QCoreApplication::setApplicationName(QStringLiteral("stock-clipper"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Stock Clipper"));

    sc::Backend backend;
    qmlRegisterSingletonInstance("StockClipper", 1, 0, "App", &backend);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("StockClipper", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // SC_OPEN=settings opens the settings sheet for screenshots.
    const QString open = qEnvironmentVariable("SC_OPEN");
    if (!open.isEmpty()) {
        QTimer::singleShot(1500, [&] {
            QObject *root = engine.rootObjects().value(0);
            if (!root)
                return;
            if (open == QLatin1String("settings"))
                root->setProperty("settingsOpen", true);
        });
    }

    // Layout smoke test: SC_SHOT=/tmp/out.png grabs the window after a moment.
    const QString shot = qEnvironmentVariable("SC_SHOT");
    if (!shot.isEmpty()) {
        QTimer::singleShot(2200, [&] {
            const QString wanted = qEnvironmentVariable("SC_CLIP");
            for (int i = 0; i < backend.clipCount(); ++i) {
                const QString path =
                    backend.clips()->data(backend.clips()->index(i, 0), sc::ClipModel::PathRole)
                        .toString();
                if (wanted.isEmpty() || path.contains(wanted)) {
                    backend.selectClip(path);
                    break;
                }
            }
            backend.seekTo(1.0);
            if (!qEnvironmentVariableIsEmpty("SC_AUTOPLAY"))
                backend.togglePlay();
            if (!qEnvironmentVariableIsEmpty("SC_RENDER"))
                backend.renderCurrent();
        });
        QTimer::singleShot(qEnvironmentVariableIntValue("SC_SHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("SC_SHOT_DELAY") : 5000, [&] {
            if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
                const QImage image = window->grabWindow();
                std::fprintf(stderr, "shot %s %dx%d\n", image.save(shot) ? "saved" : "failed",
                             image.width(), image.height());
            }
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
