#include "mobile_map_view.h"
#include "map_server_settings.h"
#include "mobile_map_settings.h"
#include "appearance_settings.h"
#include "language_settings.h"
#include "unit_profile_settings.h"
#include "location_service.h"
#include "compass_service.h"
#include "gradienter_service.h"
#include "plumb_bob_service.h"
#include "flashlight_service.h"
#include "wireguard_vpn_settings.h"
#include "embedded_map_check.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <qqml.h>
#include <QSGRendererInterface>

int main(int argc, char *argv[])
{
    // The map (AOWIS-GUI-Render) draws into Qt Quick's OpenGL / OpenGL ES
    // context from the render pass. Its frame works on the map model, the
    // tile/terrain repositories and their signals, all on the GUI thread:
    // the basic render loop renders on that thread too. Set before the
    // application object exists; an explicit environment value still wins.
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", "basic");
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // The map borrows an EGL context; X11 sessions otherwise get GLX.
    if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION"))
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
#endif
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
    parser.addOption(QCommandLineOption(QStringLiteral("check-embedded-map"),
        QStringLiteral("Check MAP storage and offline library initialization in a temporary directory.")));
    parser.process(application);
    if (parser.isSet(QStringLiteral("check-embedded-map")))
        return checkEmbeddedMap();

    qmlRegisterType<MobileMapView>("AOWIS.Map", 1, 0, "MapView");
    MapServerSettings mapServerSettings; // Legacy endpoint migration only.
    MobileMapSettings mobileMapSettings(mapServerSettings.endpointUrl(), mapServerSettings.wireguardProfileId());
    AppearanceSettings appearanceSettings;
    LanguageSettings languageSettings;
    UnitProfileSettings unitProfileSettings;
    LocationService locationService;
    CompassService compassService;
    GradienterService gradienterService;
    PlumbBobService plumbBobService;
    FlashlightService flashlightService;
    // Declared before the engine: QML bindings never outlive it, and its
    // tunnels stop when the application ends.
    WireGuardVpnSettings wireguardVpnSettings;
    wireguardVpnSettings.setProfileUsers([&mobileMapSettings](const QString &profile_id) {
        return mobileMapSettings.profileUsers(profile_id);
    });
    QObject::connect(&mobileMapSettings, &MobileMapSettings::changed,
                     &wireguardVpnSettings, &WireGuardVpnSettings::notifyUsersChanged);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mobileMapSettings"), &mobileMapSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("appearanceSettings"), &appearanceSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("languageSettings"), &languageSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("unitProfileSettings"), &unitProfileSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("locationService"), &locationService);
    engine.rootContext()->setContextProperty(QStringLiteral("compassService"), &compassService);
    engine.rootContext()->setContextProperty(QStringLiteral("gradienterService"), &gradienterService);
    engine.rootContext()->setContextProperty(QStringLiteral("plumbBobService"), &plumbBobService);
    engine.rootContext()->setContextProperty(QStringLiteral("flashlightService"), &flashlightService);
    engine.rootContext()->setContextProperty(QStringLiteral("wireguardVpnSettings"), &wireguardVpnSettings);
    // Qt Quick tracks qsTr() bindings; changing translations never reloads the
    // QML component tree or restarts any render/sensor services.
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &engine, &QQmlApplicationEngine::retranslate);
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &locationService, &LocationService::changed);
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &compassService, &CompassService::changed);
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &gradienterService, &GradienterService::changed);
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &plumbBobService, &PlumbBobService::changed);
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &flashlightService, &FlashlightService::changed);
    // Status texts are produced in C++; re-evaluate them in the new language.
    QObject::connect(&languageSettings, &LanguageSettings::languageChanged,
                     &wireguardVpnSettings, &WireGuardVpnSettings::statusRevisionChanged);
    engine.loadFromModule(QStringLiteral("AOWIS.Mobile"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return 1;

    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window == nullptr)
    {
        qCritical("QML root must be a Qt Quick Window");
        return 1;
    }
    // QML creates the window invisible: the map item has connected to the
    // window's render-pass signals before a scene graph or GL context exists.
#if defined(Q_OS_ANDROID)
    // Exercise resource teardown/recreation across Android backgrounding.
    // Qt's persistence settings are hints, not guarantees on EGL context loss.
    window->setPersistentSceneGraph(false);
    window->setPersistentGraphics(false);
#endif
    window->setIcon(QIcon(QStringLiteral(":/aowis/icon.png")));
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &locationService, [&locationService](Qt::ApplicationState state) {
        locationService.setApplicationActive(state == Qt::ApplicationActive);
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &compassService, [&compassService](Qt::ApplicationState state) {
        compassService.setApplicationActive(state == Qt::ApplicationActive);
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &gradienterService, [&gradienterService](Qt::ApplicationState state) {
        gradienterService.setApplicationActive(state == Qt::ApplicationActive);
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &plumbBobService, [&plumbBobService](Qt::ApplicationState state) {
        plumbBobService.setApplicationActive(state == Qt::ApplicationActive);
    });
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &flashlightService, [&flashlightService](Qt::ApplicationState state) {
        flashlightService.setApplicationActive(state == Qt::ApplicationActive);
    });
    window->show();
    const bool active = application.applicationState() == Qt::ApplicationActive;
    locationService.setApplicationActive(active);
    compassService.setApplicationActive(active);
    gradienterService.setApplicationActive(active);
    plumbBobService.setApplicationActive(active);
    flashlightService.setApplicationActive(active);
    return application.exec();
}
