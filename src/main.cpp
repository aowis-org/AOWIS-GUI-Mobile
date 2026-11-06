#include "render_lab.h"
#include "map_server_settings.h"
#include "appearance_settings.h"
#include "location_service.h"
#include "compass_service.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

int main(int argc, char *argv[])
{
    // Select before the first QQuickWindow is created. For Android this asks
    // Qt Quick to use OpenGL ES, for Linux desktop it requests OpenGL.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication application(argc, argv);
    // Keep the phone launcher concise, but distinguish the desktop application.
#if defined(Q_OS_ANDROID)
    const QString applicationLabel = QStringLiteral("AOWIS");
#else
    const QString applicationLabel = QStringLiteral("AOWIS Mobile");
#endif
    QCoreApplication::setApplicationName(applicationLabel);
    application.setApplicationDisplayName(applicationLabel);
    application.setWindowIcon(QIcon(QStringLiteral(":/aowis/icon.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AOWIS mobile field survey application"));
    parser.addHelpOption();
    QCommandLineOption experimental(QStringLiteral("bgfx-underlay"),
        QStringLiteral("EXPERIMENTAL: run bgfx on Qt Quick's OpenGL render thread and surface"));
    QCommandLineOption baseline(QStringLiteral("baseline"),
        QStringLiteral("Force QML-only baseline without initializing bgfx"));
    parser.addOption(experimental);
    parser.addOption(baseline);
    parser.process(application);
#if defined(Q_OS_ANDROID) && AOWIS_LAB_WITH_BGFX
    // Android launchers do not reliably forward arbitrary CLI arguments.
    // ON builds therefore exercise the experiment on device by default.
    const bool androidExperimentalDefault = true;
#else
    const bool androidExperimentalDefault = false;
#endif
    const bool useBgfx = !parser.isSet(baseline) &&
        (parser.isSet(experimental) || androidExperimentalDefault);

#if !AOWIS_LAB_WITH_BGFX
    if (useBgfx)
    {
        qCritical("This binary was built with AOWIS_MOBILE_WITH_BGFX=OFF.");
        return 2;
    }
#endif

    RenderLab lab(useBgfx);
    MapServerSettings mapServerSettings;
    AppearanceSettings appearanceSettings;
    LocationService locationService;
    CompassService compassService;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("lab"), &lab);
    engine.rootContext()->setContextProperty(QStringLiteral("mapServerSettings"), &mapServerSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("appearanceSettings"), &appearanceSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("locationService"), &locationService);
    engine.rootContext()->setContextProperty(QStringLiteral("compassService"), &compassService);
    engine.loadFromModule(QStringLiteral("AOWIS.Mobile"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return 1;

    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window == nullptr)
    {
        qCritical("QML root must be a Qt Quick Window");
        return 1;
    }
    // QML deliberately creates the window invisible. Attach all render-thread
    // callbacks BEFORE it can create a scenegraph or GL context.
#if defined(Q_OS_ANDROID)
    // Exercise resource teardown/recreation across Android backgrounding.
    // Qt's persistence settings are hints, not guarantees on EGL context loss.
    window->setPersistentSceneGraph(false);
    window->setPersistentGraphics(false);
#endif
    window->setIcon(QIcon(QStringLiteral(":/aowis/icon.png")));
    lab.attach(window);
    QObject::connect(window, &QWindow::widthChanged, &lab, [&lab](int) { lab.noteSurfaceResize(); });
    QObject::connect(window, &QWindow::heightChanged, &lab, [&lab](int) { lab.noteSurfaceResize(); });

    QTimer refresh;
    refresh.setInterval(16);
    QObject::connect(&refresh, &QTimer::timeout, window, [window]() { window->update(); });
    // The background lab refresh timer is deliberately suspended on Compass.
    // Otherwise its independent 16ms window->update() calls fight Qt Quick's
    // synchronized animation cadence and cause irregular dial frame pacing.
    QObject::connect(&compassService, &CompassService::pageVisibleChanged,
                     &refresh, [&refresh, &application, window](bool visible) {
        if (visible)
            refresh.stop();
        else if (application.applicationState() == Qt::ApplicationActive)
        {
            refresh.start();
            window->update();
        }
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &lab, [&lab, &refresh, &compassService, window](Qt::ApplicationState state) {
        const bool active = state == Qt::ApplicationActive;
        lab.setApplicationActive(active);
        if (active && !compassService.pageVisible())
        {
            refresh.start();
            window->update();
        }
        else
            refresh.stop();
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &locationService, [&locationService](Qt::ApplicationState state) {
        locationService.setApplicationActive(state == Qt::ApplicationActive);
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &compassService, [&compassService](Qt::ApplicationState state) {
        compassService.setApplicationActive(state == Qt::ApplicationActive);
    });
    window->show();
    const bool active = application.applicationState() == Qt::ApplicationActive;
    locationService.setApplicationActive(active);
    compassService.setApplicationActive(active);
    lab.setApplicationActive(active);
    if (active && !compassService.pageVisible())
        refresh.start();
    return application.exec();
}
