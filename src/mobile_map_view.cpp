#include "mobile_map_view.h"
#include "mobile_map_settings.h"

#include <QPointer>
#include <QCoreApplication>
#include <QCursor>

#if AOWIS_MOBILE_WITH_MAP

#include "lifecycle_watchdog.h"
#include "mobile_map_math.h"
#include "look_around_engine.h"
#include "look_around_camera.h"
#include "look_around_field.h"
#include "look_around_recorder.h"
#include "look_around_vision.h"
#include "compass_heading.h"
#include "wireguard_vpn_settings.h"

#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/bgfx/map_bgfx_map_view.h"
#include "map/core/map_model.h"
#include "map/core/map_provider_catalog.h"
#include "map/core/map_view_types.h"
#include "map/data/map_server_transport.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_tile_repository.h"
#include "map/render/map_render_types.h"
#include "mobile_embedded_map_backend.h"
#include "mobile_embedded_map_transport.h"
#include "mobile_map_provider_directory.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventPoint>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPalette>
#include <QPermissions>
#include <QNetworkProxy>
#include <QPointer>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QTouchEvent>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#ifdef Q_OS_ANDROID
#include <time.h>
#endif
#include <utility>
#include <vector>

namespace
{
int64_t steadyMicroseconds()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

double sensorClockSeconds()
{
#ifdef Q_OS_ANDROID
    // Android sensor timestamps include time spent in suspend.
    timespec now{};
    if (clock_gettime(CLOCK_BOOTTIME, &now) != 0)
        return -1.0;
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
#else
    return steadyMicroseconds() * 1e-6;
#endif
}

QPalette mapPalette(bool dark)
{
    QPalette palette = QGuiApplication::palette();
    const QColor window = dark ? QColor(0x10, 0x25, 0x35) : QColor(0xf3, 0xf7, 0xfa);
    const QColor text = dark ? QColor(0xf2, 0xf8, 0xfa) : QColor(0x17, 0x30, 0x40);
    const QColor button = dark ? QColor(0x25, 0x43, 0x54) : QColor(0xff, 0xff, 0xff);
    const QColor highlight = dark ? QColor(0x34, 0x6f, 0x86) : QColor(0x00, 0x6b, 0x57);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, button);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::ToolTipBase, button);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Mid, dark ? QColor(0x6c, 0x91, 0xa1) : QColor(0x9f, 0xb5, 0xc0));
    return palette;
}
}

struct MobileMapView::Impl final : public MapBgfxMapViewHost
{
    explicit Impl(MobileMapView *owner);
    ~Impl() override;

    // MapBgfxMapViewHost
    bool mapViewHudEnabled() const override { return false; }
    bool mapViewVisible() const override;
    QFont mapViewFont() const override;
    QPalette mapViewPalette() const override;
    void mapViewFocusNavigationInput(Qt::FocusReason reason) override;
    QSize mapViewLogicalSize() const override;
    qreal mapViewDevicePixelRatio() const override;
    QScreen *mapViewScreen() const override;
    QPoint mapViewGlobalPosition() const override;
    void mapViewSetCursor(Qt::CursorShape shape) override;

    QSize viewport() const;
    void attachWindow(QQuickWindow *window);
    void renderPass();
    void applyLookAroundFrame();
    void releaseContext();
    void rebuildServer();
    // Coalesces property changes (URL, profile, tunnel state) of one event
    // loop pass, so a URL is never used before its WireGuard profile is known.
    void scheduleRebuild();
    void requireTunnels();
    void releaseTunnels();
    void panBy(const QPointF &delta);
    void zoomAt(double steps, const QPointF &anchor);
    void beginOrbit();
    void endOrbit();
    void touch(QTouchEvent *event);
    void resetGestures();
    void setApplicationActive(bool active);
    void beginRecovery(const QString &reason);
    void inspectRecovery();
    void recordLifecycleEvent(const QString &event, int64_t duration_us = -1);
    void restoreCamera();
    void saveCamera();
    void refreshStatus();

    MobileMapView *item = nullptr;
    QPointer<MobileMapSettings> map_settings;
    QMetaObject::Connection map_settings_connection;
    QPointer<WireGuardVpnSettings> wireguard;
    QMetaObject::Connection wireguard_connection;
    QHash<QString, QString> required_tunnels;
    bool server_blocked = false;
    bool rebuild_pending = false;
    bool dark = false;
    std::unique_ptr<MobileEmbeddedMapBackend> backend;
    MobileMapProviderDirectory catalog;
    MapModel model;
    std::unique_ptr<MapTileRepository> tiles;
    std::unique_ptr<MapTerrainRepository> terrain;
    // Destroyed first: it holds the repositories and the model.
    std::unique_ptr<MapBgfxMapView> view;
    QPointer<QQuickWindow> window;
    std::vector<QMetaObject::Connection> window_connections;
    QString render_problem;
    QString server_status;
    bool saved_camera = false;
    QQuaternion eye_enu_to_ecef;
    QQuaternion magnetic_to_true;
    QQuaternion screen_to_device;
    AowisMobileMap::LookAroundEngine look_around_engine;
    AowisMobileMap::LookAroundFrameClock look_around_clock;
    AowisMobileMap::LookAroundFieldSource look_around_field;
    // Field mode bookkeeping (arrival clock): last field sample, when the
    // mode (re)started, samples since the last status, and the status line.
    double look_around_field_last = -1.0;
    double look_around_field_started = -1.0;
    double look_around_field_status_time = -1.0;
    int look_around_field_count = 0;
    double look_around_field_strength = 0.0;
    double look_around_field_dip = 0.0;
    QString look_around_field_status;
    // Why field readings are missing: calls from QML, of which rejected as
    // not newer, and the newest offered / last accepted sensor times.
    int look_around_field_calls = 0;
    int look_around_field_rejected = 0;
    int look_around_field_entries = 0;  // setFieldPose() calls, before any check
    QString look_around_field_guard;    // the entry check that refused the last one
    double look_around_field_offered = 0.0;
    double look_around_field_accepted = 0.0;
    void updateFieldStatus(double now, bool fallback);
    // Camera stabilization.
    bool look_around_vision = false;
    bool look_around_vision_permission_pending = false;
    std::unique_ptr<LookAroundCamera> look_around_camera;
    AowisMobileMap::VisionFusion look_around_fusion;
    int look_around_vision_frames = 0;
    int look_around_vision_valid = 0;
    int look_around_vision_points = 0;
    double look_around_vision_status_time = -1.0;
    QString look_around_vision_status;
    // Own field data drives the engine (camera stabilization).
    bool fieldDriven() const { return this->look_around_vision; }
    void configureEngine();
    void updateCamera();
    void handleVision(const AowisMobileMap::VisionTracker::Result &result);
    void setVisionStatus(const QString &status);
    // Look-around frame recording (diagnostics): the first two minutes of a
    // session, written to AppDataLocation/lookaround/ when it ends.
    struct LookAroundFrame
    {
        double wall = 0.0;
        double paced = 0.0;
        double core = 0.0;
        double delay = 0.0;
        double bearing = 0.0;
        double pitch = 0.0;
        double heading_noise = 0.0;
        double heading_correction = 0.0;
        int gyro_samples = 0;
        bool uses_gyro = false;
        int mode_switches = 0;
        bool field_motion = false;
        bool field_trusted = true;
        bool panorama = false;
    };
    static constexpr std::size_t kLookAroundRecordFrames = 7200;
    std::vector<LookAroundFrame> look_around_frames;
    int look_around_gyro_samples = 0;
    double look_around_last_gyro = 0.0;
    bool look_around_panorama = false;
    void resetLookAround();
    void writeLookAroundRecording();

    bool has_marker = false;
    bool marker_fresh = false;
    QPointF marker_coordinate;

    void drawMarker(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y) const
    {
        canvas->setWhiteTexel(cache->whiteU(), cache->whiteV());
        canvas->fillCircle(x, y, 8.0, 0xff263544u);
        canvas->fillCircle(x, y, 7.0, 0xffffffffu);
        canvas->fillCircle(x, y, 5.0, this->marker_fresh ? 0xff007bc2u : 0xff77838bu);
    }

    // Gestures.
    enum class Pointer { None, Pan, Orbit };
    Pointer mouse = Pointer::None;
    QPointF mouse_last;
    enum class Touch { None, Single, Multi };
    Touch touch_mode = Touch::None;
    int touch_first_id = -1;
    int touch_second_id = -1;
    QPointF touch_last;
    QPointF touch_centroid;
    double touch_distance = 0.0;
    double touch_angle = 0.0;
    bool orbiting = false;
    QPointF pan_remainder;
    QPointF tilt_remainder;

    QTimer motion_timer;
    int64_t last_tick = 0;
    int64_t last_drag = 0;
    QPointF pan_velocity;
    double pinch_velocity = 0.0;
    double zoom_remaining = 0.0;
    QPointF zoom_anchor;
    bool follow_fix = false;
    int64_t return_start = 0;
    int64_t north_start = 0;
    double north_start_angle = 0.0;
    QPointF return_origin;
    QPointF gesture_pan;
    int64_t older_return_us = 0;
    int64_t newer_return_us = 0;
    int64_t hint_after_us = 0;

    void stopMotion();
    void sampleDrag(const QPointF &delta, double zoom_steps = 0.0);
    void releaseMotion();
    void startMotion();
    void returnToFix();
    void advanceMotion();

    // Lifecycle (first frame after resume, scene-graph recreation, resize).
    AowisMobileLifecycle::Watchdog watchdog;
    QString recovery_reason;
    QTimer status_timer;
    bool application_active = false;
    bool was_initialized = false;
    quint64 frames = 0;
    quint32 scene_graph_starts = 0;
    quint32 scene_graph_releases = 0;
    quint32 bgfx_initializations = 0;
    quint32 recoveries = 0;
    quint32 timeouts = 0;
    int64_t last_frame_us = 0;
    int64_t last_recovery_us = -1;
};

MobileMapView::Impl::Impl(MobileMapView *owner)
    : item(owner), model(&this->catalog)
{
    this->view = std::make_unique<MapBgfxMapView>(
        &this->model, this, MapBgfxMapView::RenderTarget::HostContext);
    // No hydraulic network on Mobile yet: imagery and terrain.
    this->view->setDistributionNetworkVisible(false);
    QObject::connect(this->view.get(), &MapBgfxMapView::hostFrameRequested, this->item, [this]()
    {
        if (this->window != nullptr)
            this->window->update();
    });
    QObject::connect(this->view.get(), &MapBgfxMapView::signalRendererFailed, this->item,
                     [this](const QString &reason)
    {
        this->render_problem = reason;
        qWarning().noquote() << "AOWIS map:" << reason;
        emit this->item->statusChanged();
    });
    QObject::connect(this->view.get(), &MapBgfxMapView::signalRendererReady, this->item, [this]()
    {
        qInfo().noquote() << "AOWIS map: first frame," << this->view->graphicsApiName();
        emit this->item->statusChanged();
    });

    // Server providers: the model re-resolves its provider, the tiles their
    // provider URLs.
    QObject::connect(&this->catalog, &MobileMapProviderDirectory::catalogChanged, this->item, [this]()
    {
        this->model.providerCatalogChanged();
        emit this->item->providersChanged();
        if (this->tiles)
            this->tiles->invalidateProviderCatalog();
        if (!this->server_blocked && this->backend && this->backend->settingsError().isEmpty() &&
            this->backend->terrainReady())
            this->server_status = MobileMapView::tr("Local map: %1 imagery providers").arg(this->catalog.providers().size());
        emit this->item->statusChanged();
    });
    QObject::connect(&this->model, &MapModel::providerIdChanged, this->item,
                     &MobileMapView::providerIdChanged);
    QObject::connect(&this->model, &MapModel::centerChangedWGS84, this->item,
                     [this](CoordinateWGS84) { emit this->item->cameraChanged(); });
    QObject::connect(&this->model, &MapModel::view2dContinuousZoomChanged, this->item,
                     [this](double) { emit this->item->cameraChanged(); });
    QObject::connect(&this->model, &MapModel::viewGlobeCameraChanged, this->item,
                     &MobileMapView::northAngleChanged);
    QObject::connect(&this->model, &MapModel::viewModeChanged, this->item,
                     [this](MapViewMode) { emit this->item->cameraChanged(); });

    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, this->item,
                     [this](Qt::ApplicationState state)
    {
        setApplicationActive(state == Qt::ApplicationActive);
    });
    this->motion_timer.setInterval(16);
    this->motion_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&this->motion_timer, &QTimer::timeout, this->item, [this]() { advanceMotion(); });
    this->status_timer.setInterval(250);
    QObject::connect(&this->status_timer, &QTimer::timeout, this->item, [this]()
    {
        inspectRecovery();
        emit this->item->statusChanged();
    });
    this->status_timer.start();
    restoreCamera();
    this->server_status = MobileMapView::tr("Initializing local map");
}

MobileMapView::Impl::~Impl()
{
    if (this->look_around_camera)
        this->look_around_camera->stop();
    saveCamera();
    for (const QMetaObject::Connection &connection : this->window_connections)
        QObject::disconnect(connection);
}

// --- MapBgfxMapViewHost ------------------------------------------------------------

bool MobileMapView::Impl::mapViewVisible() const
{
    return this->item->isVisible() && this->window != nullptr && this->window->isVisible()
        && this->item->width() > 0.0 && this->item->height() > 0.0;
}

QFont MobileMapView::Impl::mapViewFont() const
{
    return QGuiApplication::font();
}

QPalette MobileMapView::Impl::mapViewPalette() const
{
    return mapPalette(this->dark);
}

void MobileMapView::Impl::mapViewFocusNavigationInput(Qt::FocusReason reason)
{
    this->item->forceActiveFocus(reason);
}

QSize MobileMapView::Impl::mapViewLogicalSize() const
{
    return viewport();
}

qreal MobileMapView::Impl::mapViewDevicePixelRatio() const
{
    return this->window != nullptr ? this->window->effectiveDevicePixelRatio() : 1.0;
}

QScreen *MobileMapView::Impl::mapViewScreen() const
{
    return this->window != nullptr ? this->window->screen() : nullptr;
}

QPoint MobileMapView::Impl::mapViewGlobalPosition() const
{
    return this->item->mapToGlobal(QPointF(0.0, 0.0)).toPoint();
}

void MobileMapView::Impl::mapViewSetCursor(Qt::CursorShape shape)
{
    if (shape == Qt::ArrowCursor)
        this->item->unsetCursor();
    else
        this->item->setCursor(shape);
}

// --- Rendering ----------------------------------------------------------------------

QSize MobileMapView::Impl::viewport() const
{
    return QSize(std::max(1, qRound(this->item->width())), std::max(1, qRound(this->item->height())));
}

void MobileMapView::Impl::attachWindow(QQuickWindow *new_window)
{
    if (this->window == new_window)
        return;
    for (const QMetaObject::Connection &connection : this->window_connections)
        QObject::disconnect(connection);
    this->window_connections.clear();
    this->window = new_window;
    if (new_window == nullptr)
        return;
    this->window_connections.push_back(QObject::connect(
        new_window, &QQuickWindow::afterAnimating, this->item,
        [this]() { applyLookAroundFrame(); }));
    // Qt Quick's basic render loop (main.cpp): these run on the GUI thread,
    // the map view's thread, with Qt's OpenGL context current.
    this->window_connections.push_back(QObject::connect(
        new_window, &QQuickWindow::beforeRenderPassRecording, this->item,
        [this]() { renderPass(); }, Qt::DirectConnection));
    this->window_connections.push_back(QObject::connect(
        new_window, &QQuickWindow::sceneGraphInvalidated, this->item,
        [this]() { releaseContext(); }, Qt::DirectConnection));
    this->window_connections.push_back(QObject::connect(
        new_window, &QQuickWindow::sceneGraphInitialized, this->item, [this]()
    {
        ++this->scene_graph_starts;
    }, Qt::DirectConnection));
    this->window_connections.push_back(QObject::connect(
        new_window, &QWindow::widthChanged, this->item, [this](int) { beginRecovery(QStringLiteral("resize")); }));
    this->window_connections.push_back(QObject::connect(
        new_window, &QWindow::heightChanged, this->item, [this](int) { beginRecovery(QStringLiteral("resize")); }));
    new_window->update();
}

void MobileMapView::Impl::renderPass()
{
    if (this->window == nullptr || !mapViewVisible())
        return;
    if (this->window->rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL)
    {
        this->render_problem = MobileMapView::tr("Qt Quick is not using OpenGL; the map needs an OpenGL or OpenGL ES scene graph");
        return;
    }
    QOpenGLContext *context = QOpenGLContext::currentContext();
    const QNativeInterface::QEGLContext *egl = context != nullptr
        ? context->nativeInterface<QNativeInterface::QEGLContext>() : nullptr;
    if (egl == nullptr || egl->nativeContext() == nullptr)
    {
        this->render_problem = MobileMapView::tr("No EGL context available (GLX is not supported)");
        return;
    }
    MapBgfxMapView::HostContext host_context;
    host_context.native_context = static_cast<void *>(egl->nativeContext());
    host_context.opengl_es = context->isOpenGLES();
    this->window->beginExternalCommands();
    const bool drawn = this->view->renderHostFrame(host_context);
    this->window->endExternalCommands();
    const bool initialized = this->view->graphicsApiName() != QStringLiteral("bgfx (not initialized)");
    if (initialized && !this->was_initialized)
    {
        ++this->bgfx_initializations;
        recordLifecycleEvent(QStringLiteral("bgfx_initialized"));
    }
    this->was_initialized = initialized;
    if (drawn)
    {
        ++this->frames;
        this->last_frame_us = steadyMicroseconds();
    }
}

void MobileMapView::Impl::releaseContext()
{
    ++this->scene_graph_releases;
    const bool context_current = QOpenGLContext::currentContext() != nullptr;
    if (!context_current)
        qWarning("AOWIS map: scene graph released without a current OpenGL context");
    this->view->releaseHostContext();
    this->was_initialized = false;
    recordLifecycleEvent(QStringLiteral("scene_graph_released"));
    if (this->application_active)
        beginRecovery(QStringLiteral("scene_graph_recreate"));
}

// --- Map server ------------------------------------------------------------------------

void MobileMapView::Impl::scheduleRebuild()
{
    if (this->rebuild_pending)
        return;
    this->rebuild_pending = true;
    QTimer::singleShot(0, this->item, [this]()
    {
        this->rebuild_pending = false;
        this->rebuildServer();
    });
}

void MobileMapView::Impl::releaseTunnels()
{
    if (this->wireguard)
        for (const QString &consumer : this->required_tunnels.keys())
            this->wireguard->require(consumer, QString());
    this->required_tunnels.clear();
}

void MobileMapView::Impl::requireTunnels()
{
    QHash<QString, QString> desired;
    if (this->map_settings && this->map_settings->ready())
        for (const Aowis::Map::MapCascadeUpstream &upstream : this->map_settings->cascadeConfig().upstreams)
            if (!upstream.wireguard_profile_id.isEmpty())
                desired.insert(QStringLiteral("map-upstream/") + upstream.name, upstream.wireguard_profile_id);
    if (this->wireguard)
    {
        // Acquire new consumers before releasing old ones sharing a profile.
        for (QHash<QString, QString>::const_iterator it = desired.cbegin(); it != desired.cend(); ++it)
            this->wireguard->require(it.key(), it.value());
        for (const QString &consumer : this->required_tunnels.keys())
            if (!desired.contains(consumer)) this->wireguard->require(consumer, QString());
    }
    this->required_tunnels = desired;
}

void MobileMapView::Impl::rebuildServer()
{
    // Keep repositories and cached rendering data alive across tunnel changes.
    const bool ready = this->map_settings && this->map_settings->ready();
    Aowis::Map::MapCascadeConfig cascade;
    cascade.allow_direct_fetch = false;
    if (ready) cascade = this->map_settings->cascadeConfig();
    this->requireTunnels();
    bool usable = false;
    QString reason;
    for (Aowis::Map::MapCascadeUpstream &upstream : cascade.upstreams)
    {
        if (Aowis::Map::requiresWireGuardTransport(upstream))
        {
            QNetworkProxy proxy;
            if (!this->wireguard)
                reason = MobileMapView::tr("The map server is configured for WireGuard, which is not available.");
            else if (this->wireguard->proxyFor(upstream.wireguard_profile_id, &proxy, &reason))
                upstream.proxy = proxy;
        }
        usable = usable || Aowis::Map::cascadeUpstreamUsable(upstream);
    }
    this->server_blocked = !cascade.upstreams.isEmpty() && !usable && !cascade.allow_direct_fetch;
    if (!this->backend)
    {
        this->backend = std::make_unique<MobileEmbeddedMapBackend>();
        QObject::connect(this->backend.get(), &MobileEmbeddedMapBackend::providersChanged,
                         this->item, [this]() {
            this->catalog.replace(this->backend->availableProviders());
            if (this->map_settings) this->map_settings->setAvailable(this->backend->availableProviders());
        });
        MobileEmbeddedMapBackend *backend = this->backend.get();
        MapServerTransport transport;
        transport.create = [backend](QObject *parent) -> InterfaceServerMap *
        {
            return new MobileEmbeddedMapTransport(*backend, parent);
        };
        this->tiles = std::make_unique<MapTileRepository>(transport);
        this->terrain = std::make_unique<MapTerrainRepository>(transport);
        this->view->setTileRepository(this->tiles.get());
        this->view->setTerrainRepository(this->terrain.get());
    }
    this->backend->configure(cascade, ready);
    if (!this->backend->settingsError().isEmpty())
        this->server_status = MobileMapView::tr("Map server error: %1").arg(this->backend->settingsError());
    else if (this->server_blocked)
        this->server_status = reason;
    else if (!this->backend->terrainReady())
        this->server_status = MobileMapView::tr("Map server error: %1").arg(this->backend->terrainError());
    else
        this->server_status = MobileMapView::tr("Local map: %1 imagery providers").arg(this->catalog.providers().size());
    emit this->item->statusChanged();
}

void MobileMapView::Impl::panBy(const QPointF &delta)
{
    // Whole pixels for the model; the remainder carries to the next move.
    this->pan_remainder += delta;
    const QPoint step(qRound(this->pan_remainder.x()), qRound(this->pan_remainder.y()));
    if (step.isNull())
        return;
    this->pan_remainder -= QPointF(step);
    const QSize size = viewport();
    if (this->model.viewMode() != MapViewMode::Globe)
    {
        this->model.panByPixels(step, size);
        return;
    }
    // The ground under the crosshair follows the finger: the globe turns on
    // the sphere through the drawn surface there (as the desktop map).
    double crosshair_radius_m = 0.0;
    GeoWgs84Ellipsoid::EcefPositionD crosshair_point;
    if (this->view->globeSurfacePointAtScreen(QPointF(size.width() * 0.5, size.height() * 0.5),
                                              &crosshair_point))
    {
        crosshair_radius_m = std::sqrt(crosshair_point.x * crosshair_point.x
                                       + crosshair_point.y * crosshair_point.y
                                       + crosshair_point.z * crosshair_point.z);
    }
    this->model.panByPixelsGlobe(step, size, crosshair_radius_m);
}

void MobileMapView::Impl::zoomAt(double steps, const QPointF &anchor)
{
    if (!std::isfinite(steps) || steps == 0.0)
        return;
    if (this->model.globeEyePose())
    {
        this->model.zoomGlobeEyeBy(steps);
        return;
    }
    const QSize size = viewport();
    if (this->model.viewMode() == MapViewMode::Globe)
        this->model.setViewGlobeZoomLevel(this->model.viewGlobeZoomLevel(size) + steps, size);
    else
        this->model.zoomByAt(steps, anchor, size);
}

void MobileMapView::Impl::stopMotion()
{
    this->motion_timer.stop();
    this->north_start = 0;
    this->pan_velocity = {};
    this->pinch_velocity = 0.0;
    this->zoom_remaining = 0.0;
    this->return_start = 0;
    this->last_drag = steadyMicroseconds();
    this->gesture_pan = {};
}

void MobileMapView::Impl::sampleDrag(const QPointF &delta, double zoom_steps)
{
    this->gesture_pan += delta;
    const int64_t now = steadyMicroseconds();
    const double dt = std::clamp((now - this->last_drag) * 1e-6, 0.008, 0.12);
    this->last_drag = now;
    const double weight = 1.0 - std::exp(-dt / 0.035);
    this->pan_velocity += (delta / dt - this->pan_velocity) * weight;
    this->pinch_velocity += (zoom_steps / dt - this->pinch_velocity) * weight;
}

void MobileMapView::Impl::startMotion()
{
    if (!this->motion_timer.isActive() && this->application_active && this->item->isVisible())
    {
        this->last_tick = steadyMicroseconds();
        this->motion_timer.start();
    }
}

void MobileMapView::Impl::returnToFix()
{
    if (this->model.globeEyePose() || !this->follow_fix || this->touch_mode != Touch::None || this->mouse != Pointer::None)
        return;
    this->pan_velocity = {};
    this->return_origin = QPointF(this->model.centerLon(), this->model.centerLat());
    this->return_start = steadyMicroseconds();
    startMotion();
}

void MobileMapView::Impl::releaseMotion()
{
    if (steadyMicroseconds() - this->last_drag > 90000)
    {
        this->pan_velocity = {};
        this->pinch_velocity = 0.0;
    }
    this->zoom_remaining += std::clamp(this->pinch_velocity * 0.12, -0.75, 0.75);
    this->pinch_velocity = 0.0;
    returnToFix();
    const int64_t now = this->return_start;
    // Count deliberate pan releases only. Fix updates also start a return.
    if (now != 0 && now >= this->hint_after_us
        && std::hypot(this->gesture_pan.x(), this->gesture_pan.y()) >= 12.0
        && this->return_origin != this->marker_coordinate)
    {
        if (this->older_return_us != 0 && now - this->older_return_us <= 8000000)
        {
            emit this->item->repeatedGpsReturn();
            this->hint_after_us = now + 60000000;
        }
        this->older_return_us = this->newer_return_us;
        this->newer_return_us = now;
    }
    this->gesture_pan = {};
    startMotion();
}

void MobileMapView::Impl::advanceMotion()
{
    const int64_t now = steadyMicroseconds();
    const double dt = std::clamp((now - this->last_tick) * 1e-6, 0.0, 0.05);
    this->last_tick = now;
    const double decay = std::exp(-dt / 0.17);
    const bool dragging = this->mouse != Pointer::None || this->touch_mode != Touch::None;
    if (!dragging && std::hypot(this->pan_velocity.x(), this->pan_velocity.y()) >= 5.0)
    {
        panBy(this->pan_velocity * (0.17 * (1.0 - decay)));
        this->pan_velocity *= decay;
    }
    else if (!dragging)
        this->pan_velocity = {};

    const double zoom_step = std::abs(this->zoom_remaining) < 0.001 ? this->zoom_remaining
        : this->zoom_remaining * (1.0 - std::exp(-dt / 0.085));
    if (zoom_step != 0.0)
    {
        const QSize size = viewport();
        const QPointF anchor = this->follow_fix ? QPointF(size.width() * 0.5, size.height() * 0.5)
                                                : this->zoom_anchor;
        zoomAt(zoom_step, anchor);
        this->zoom_remaining -= zoom_step;
    }

    if (this->return_start != 0)
    {
        const double remaining = std::clamp(1.0 - (now - this->return_start) / 420000.0, 0.0, 1.0);
        const double fraction = 1.0 - remaining * remaining * remaining;
        const double lon_delta = std::remainder(this->marker_coordinate.x() - this->return_origin.x(), 360.0);
        this->model.setCenter(std::remainder(this->return_origin.x() + fraction * lon_delta, 360.0),
            this->return_origin.y() + fraction * (this->marker_coordinate.y() - this->return_origin.y()), viewport());
        if (remaining == 0.0)
            this->return_start = 0;
    }
    if (this->north_start != 0)
    {
        const double remaining = std::clamp(1.0 - (now - this->north_start) / 360000.0, 0.0, 1.0);
        this->model.setViewGlobeYawDeg(this->north_start_angle * remaining * remaining * remaining);
        if (remaining == 0.0)
            this->north_start = 0;
    }
    if (this->north_start == 0 && this->return_start == 0 && (dragging || this->pan_velocity.isNull()) && this->zoom_remaining == 0.0)
        this->motion_timer.stop();
}

void MobileMapView::Impl::beginOrbit()
{
    if (this->orbiting || this->model.viewMode() != MapViewMode::Globe)
        return;
    this->orbiting = true;
    this->tilt_remainder = QPointF();
    this->model.beginView3dRotateInteraction();
}

void MobileMapView::Impl::endOrbit()
{
    if (!this->orbiting)
        return;
    this->orbiting = false;
    this->model.endView3dRotateInteraction();
}

void MobileMapView::Impl::resetGestures()
{
    stopMotion();
    endOrbit();
    this->touch_mode = Touch::None;
    this->touch_first_id = -1;
    this->touch_second_id = -1;
    this->mouse = Pointer::None;
    this->pan_remainder = QPointF();
}

void MobileMapView::Impl::touch(QTouchEvent *event)
{
    if (event->type() == QEvent::TouchCancel)
    {
        resetGestures();
        returnToFix();
        return;
    }
    // The fingers still down, in press order.
    std::vector<QEventPoint> down;
    for (const QEventPoint &point : event->points())
    {
        if (point.state() != QEventPoint::Released)
            down.push_back(point);
    }
    std::sort(down.begin(), down.end(), [](const QEventPoint &a, const QEventPoint &b)
    {
        return a.id() < b.id();
    });

    if (down.empty())
    {
        endOrbit();
        this->touch_mode = Touch::None;
        releaseMotion();
        return;
    }
    if (down.size() == 1)
    {
        const QEventPoint &finger = down.front();
        if (this->touch_mode == Touch::None)
        {
            stopMotion();
            this->touch_first_id = finger.id();
            this->touch_last = finger.position();
            this->touch_mode = Touch::Single;
            this->pan_remainder = QPointF();
            return;
        }
        if (this->touch_mode == Touch::Multi || finger.id() != this->touch_first_id)
        {
            // A finger of a pinch lifted: continue panning without a jump.
            endOrbit();
            this->touch_mode = Touch::Single;
            this->pan_velocity = {};
            this->touch_first_id = finger.id();
            this->touch_last = finger.position();
            this->pan_remainder = QPointF();
            return;
        }
        if (this->model.globeEyePose())
            return;
        sampleDrag(finger.position() - this->touch_last);
        panBy(finger.position() - this->touch_last);
        this->touch_last = finger.position();
        return;
    }
    const QPointF p0 = down[0].position();
    const QPointF p1 = down[1].position();
    const QPointF centroid = (p0 + p1) * 0.5;
    const double distance = AowisMobileMap::pointDistance(p0.x(), p0.y(), p1.x(), p1.y());
    const double angle = AowisMobileMap::pointAngleDegrees(p0.x(), p0.y(), p1.x(), p1.y());
    if (this->touch_mode != Touch::Multi || down[0].id() != this->touch_first_id
        || down[1].id() != this->touch_second_id)
    {
        stopMotion();
        this->touch_mode = Touch::Multi;
        this->touch_first_id = down[0].id();
        this->touch_second_id = down[1].id();
        this->touch_centroid = centroid;
        this->touch_distance = distance;
        this->touch_angle = angle;
        this->pan_remainder = QPointF();
        if (!this->model.globeEyePose())
            beginOrbit();
        return;
    }
    const double zoom_steps = this->touch_distance > 1.0 && distance > 1.0
        ? AowisMobileMap::zoomStepsForScale(distance / this->touch_distance) : 0.0;
    this->zoom_anchor = centroid;
    zoomAt(zoom_steps, centroid);
    if (this->model.globeEyePose())
    {
        sampleDrag(QPointF(), zoom_steps);
        this->touch_distance = distance;
        return;
    }
    const QPointF moved = centroid - this->touch_centroid;
    sampleDrag(this->model.viewMode() == MapViewMode::Globe ? QPointF() : moved, zoom_steps);
    if (this->model.viewMode() == MapViewMode::Globe)
    {
        // Two fingers on the globe: turn with the fingers, tilt by moving
        // them up / down together.
        const double turn = AowisMobileMap::angleDeltaDegrees(this->touch_angle, angle);
        if (turn != 0.0)
            this->model.orbitViewGlobe(turn, 0.0);
        this->tilt_remainder += QPointF(0.0, moved.y());
        const int tilt = qRound(this->tilt_remainder.y());
        if (tilt != 0)
        {
            this->tilt_remainder.setY(this->tilt_remainder.y() - tilt);
            this->model.orbitViewGlobeByPointerDelta(QPoint(0, tilt), true);
        }
    }
    else
    {
        panBy(moved);
    }
    this->touch_centroid = centroid;
    this->touch_distance = distance;
    this->touch_angle = angle;
}

// --- Lifecycle ----------------------------------------------------------------------------

void MobileMapView::Impl::setApplicationActive(bool active)
{
    if (this->application_active == active)
        return;
    this->application_active = active;
    resetLookAround();
    updateCamera();
    recordLifecycleEvent(active ? QStringLiteral("app_active") : QStringLiteral("app_inactive"));
    if (active)
    {
        returnToFix();
        beginRecovery(QStringLiteral("resume"));
        if (this->window != nullptr)
            this->window->update();
    }
    else
    {
        this->watchdog.cancel();
        resetGestures();
        saveCamera();
    }
}

void MobileMapView::Impl::beginRecovery(const QString &reason)
{
    if (!this->application_active || !mapViewVisible())
        return;
    // Width and height change separately on rotation: one watch per change.
    if (reason == this->recovery_reason && this->watchdog.pending())
        return;
    this->recovery_reason = reason;
    this->watchdog.arm(steadyMicroseconds());
    recordLifecycleEvent(QStringLiteral("watch_") + reason);
}

void MobileMapView::Impl::inspectRecovery()
{
    const AowisMobileLifecycle::Outcome outcome = this->watchdog.poll(
        steadyMicroseconds(), this->last_frame_us, this->application_active,
        this->window != nullptr && this->window->isExposed() && mapViewVisible());
    if (outcome == AowisMobileLifecycle::Outcome::Recovered
        || outcome == AowisMobileLifecycle::Outcome::LateRecovered)
    {
        ++this->recoveries;
        this->last_recovery_us = this->watchdog.lastDurationUs();
        recordLifecycleEvent(outcome == AowisMobileLifecycle::Outcome::LateRecovered
                                 ? QStringLiteral("late_recovery") : QStringLiteral("recovered"),
                             this->last_recovery_us);
    }
    else if (outcome == AowisMobileLifecycle::Outcome::TimedOut)
    {
        ++this->timeouts;
        recordLifecycleEvent(QStringLiteral("timeout"));
    }
}

void MobileMapView::Impl::recordLifecycleEvent(const QString &event, int64_t duration_us)
{
    // Read by tools/test_android_lifecycle.sh (adb run-as); event names are
    // constants, never translated.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/lifecycle");
    if (!QDir().mkpath(directory))
        return;
    QFile file(directory + QStringLiteral("/events.csv"));
    const bool first = !file.exists() || file.size() == 0;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&file);
    if (first)
        stream << "utc,event,duration_us,app_active,exposed,frames,sg_init,sg_release,bgfx_init,width,height\n";
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ',' << event << ','
           << duration_us << ',' << (this->application_active ? 1 : 0) << ','
           << (this->window != nullptr && this->window->isExposed() ? 1 : 0) << ','
           << this->frames << ',' << this->scene_graph_starts << ',' << this->scene_graph_releases
           << ',' << this->bgfx_initializations << ',' << viewport().width() << ','
           << viewport().height() << '\n';
    stream.flush();
    qInfo().noquote() << "AOWIS lifecycle:" << event << "duration_us=" << duration_us
                      << "bgfx_inits=" << this->bgfx_initializations << "frames=" << this->frames;
}

void MobileMapView::Impl::restoreCamera()
{
    QSettings settings(QSettings::NativeFormat, QSettings::UserScope,
                       QStringLiteral("AOWIS"), QStringLiteral("AOWIS-GUI-Mobile"));
    bool lon_valid = false;
    bool lat_valid = false;
    bool zoom_valid = false;
    const double lon = settings.value(QStringLiteral("map/longitude")).toDouble(&lon_valid);
    const double lat = settings.value(QStringLiteral("map/latitude")).toDouble(&lat_valid);
    const double zoom = settings.value(QStringLiteral("map/zoom")).toDouble(&zoom_valid);
    if (lon_valid && lat_valid && zoom_valid && std::isfinite(lon) && std::isfinite(lat)
        && std::isfinite(zoom))
    {
        this->saved_camera = true;
        this->model.setView2dContinuousZoom(zoom);
        this->model.setCenter(lon, lat);
        return;
    }
    // Before the first fix: Central Europe, the whole region in view.
    this->model.setView2dContinuousZoom(4.0);
    this->model.setCenter(10.0, 50.0);
}

void MobileMapView::Impl::saveCamera()
{
    QSettings settings(QSettings::NativeFormat, QSettings::UserScope,
                       QStringLiteral("AOWIS"), QStringLiteral("AOWIS-GUI-Mobile"));
    settings.setValue(QStringLiteral("map/longitude"), this->model.centerLon());
    settings.setValue(QStringLiteral("map/latitude"), this->model.centerLat());
    settings.setValue(QStringLiteral("map/zoom"), this->model.view2dContinuousZoom());
}

// --- MobileMapView -------------------------------------------------------------------------

MobileMapView::MobileMapView(QQuickItem *parent)
    : QQuickItem(parent), impl(std::make_unique<Impl>(this))
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptTouchEvents(true);
    setFlag(QQuickItem::ItemIsFocusScope, true);
    this->impl->application_active = qGuiApp->applicationState() == Qt::ApplicationActive;
    this->impl->scheduleRebuild();
}

MobileMapView::~MobileMapView()
{
    this->impl->releaseTunnels();
}

QObject *MobileMapView::mapSettings() const { return this->impl->map_settings; }
void MobileMapView::setMapSettings(QObject *settings)
{
    MobileMapSettings *typed = qobject_cast<MobileMapSettings *>(settings);
    if (this->impl->map_settings == typed) return;
    QObject::disconnect(this->impl->map_settings_connection);
    this->impl->map_settings = typed;
    if (typed)
        this->impl->map_settings_connection = connect(typed, &MobileMapSettings::changed, this,
            [this]() { this->impl->scheduleRebuild(); });
    this->impl->scheduleRebuild();
    emit mapSettingsChanged();
}

QObject *MobileMapView::wireguard() const
{
    return this->impl->wireguard;
}

void MobileMapView::setWireguard(QObject *settings)
{
    WireGuardVpnSettings *wireguard = qobject_cast<WireGuardVpnSettings *>(settings);
    if (this->impl->wireguard == wireguard)
        return;
    this->impl->releaseTunnels();
    QObject::disconnect(this->impl->wireguard_connection);
    this->impl->wireguard = wireguard;
    if (wireguard != nullptr)
    {
        this->impl->wireguard_connection = connect(wireguard, &WireGuardVpnSettings::tunnelAvailabilityChanged, this,
                                                   [this](const QString &profile_id)
        {
            if (this->impl->required_tunnels.values().contains(profile_id))
                this->impl->scheduleRebuild();
        });
    }
    this->impl->requireTunnels();
    this->impl->scheduleRebuild();
    emit wireguardChanged();
}

bool MobileMapView::serverBlocked() const
{
    return this->impl->server_blocked;
}

bool MobileMapView::darkTheme() const
{
    return this->impl->dark;
}

void MobileMapView::setDarkTheme(bool dark)
{
    if (this->impl->dark == dark)
        return;
    this->impl->dark = dark;
    this->impl->view->handleHostChange(QEvent::PaletteChange);
    this->impl->view->requestRedraw();
    emit darkThemeChanged();
}

bool MobileMapView::rendererAvailable() const
{
    return true;
}

bool MobileMapView::savedCamera() const
{
    return this->impl->saved_camera;
}

bool MobileMapView::globe() const
{
    return this->impl->model.viewMode() == MapViewMode::Globe;
}

double MobileMapView::centerLongitude() const
{
    return this->impl->model.centerLon();
}

double MobileMapView::centerLatitude() const
{
    return this->impl->model.centerLat();
}

QString MobileMapView::status() const
{
    if (!this->impl->render_problem.isEmpty())
        return this->impl->render_problem;
    return tr("Map renderer: %1").arg(this->impl->view->graphicsApiName());
}

QString MobileMapView::serverStatus() const
{
    return this->impl->server_status;
}

QString MobileMapView::diagnostics() const
{
    return this->impl->view->startupDiagnostics();
}

QString MobileMapView::lifecycle() const
{
    return tr("Map frames: %1 | scene graph: %2 inits / %3 releases | bgfx inits: %4\n"
              "Recoveries: %5 | stalled: %6 | last recovery: %7 ms")
        .arg(this->impl->frames).arg(this->impl->scene_graph_starts)
        .arg(this->impl->scene_graph_releases).arg(this->impl->bgfx_initializations)
        .arg(this->impl->recoveries).arg(this->impl->timeouts)
        .arg(this->impl->last_recovery_us < 0 ? QStringLiteral("-")
             : QString::number(double(this->impl->last_recovery_us) / 1000.0, 'f', 1));
}

void MobileMapView::centerOn(double longitude_deg, double latitude_deg, double zoom)
{
    if (!std::isfinite(longitude_deg) || !std::isfinite(latitude_deg))
        return;
    const QSize size = this->impl->viewport();
    if (this->impl->model.viewMode() != MapViewMode::Globe && std::isfinite(zoom))
        this->impl->model.setView2dContinuousZoom(zoom, size);
    this->impl->model.setCenter(longitude_deg, latitude_deg, size);
}

void MobileMapView::setLocationMarker(bool valid, double longitude, double latitude, bool fresh, bool follow)
{
    const QPointF coordinate(longitude, latitude);
    const bool follow_changed = this->impl->follow_fix != follow;
    this->impl->follow_fix = follow;
    if (!follow)
    {
        this->impl->return_start = 0;
        this->impl->older_return_us = 0;
        this->impl->newer_return_us = 0;
    }
    if (!follow_changed && this->impl->has_marker == valid &&
        (!valid || (this->impl->marker_coordinate == coordinate && this->impl->marker_fresh == fresh)))
        return;
    const bool had_marker = this->impl->has_marker;
    this->impl->has_marker = valid;
    this->impl->marker_coordinate = coordinate;
    this->impl->marker_fresh = fresh;
    this->impl->returnToFix();
    if (!valid)
    {
        this->impl->view->setOverlayBuilder({});
        this->impl->view->setGlobeOverlayBuilder({});
    }
    else if (!had_marker)
    {
        this->impl->view->setOverlayBuilder([this](MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
            const MapBgfx2d::Camera &camera, const QSizeF &)
        {
            canvas->clear();
            const QPointF coord = this->impl->marker_coordinate;
            const MapBgfx2d::ScreenPoint point = MapBgfx2d::screenFromWorld(camera,
                MapBgfx2d::worldFromWgs84(coord.x(), coord.y()));
            this->impl->drawMarker(canvas, cache, point.x, point.y);
            return QPointF();
        });
        this->impl->view->setGlobeOverlayBuilder([this](MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
            const MapBgfxMapView::GlobeProjector &project, const MapBgfxMapView::GlobeElevatedProjector &,
            const QSizeF &, const MapBgfxGlobeIconIndex::Frustum &)
        {
            canvas->clear();
            const QPointF coord = this->impl->marker_coordinate;
            QPointF point;
            if (project(coord.x(), coord.y(), &point, nullptr))
                this->impl->drawMarker(canvas, cache, point.x(), point.y());
        });
    }
    this->impl->view->requestRedraw();
}

bool MobileMapView::globeAvailable() const
{
    return this->impl->view->globeAvailable();
}

QVariantList MobileMapView::providers() const
{
    QVariantList result;
    for (const MapProviderEntry &entry : this->impl->catalog.providers())
    {
        if (entry.enabled)
            result.append(QVariantMap{{QStringLiteral("id"), entry.id},
                                     {QStringLiteral("name"), entry.name.isEmpty() ? entry.id : entry.name}});
    }
    return result;
}

QString MobileMapView::providerId() const
{
    return this->impl->model.providerId();
}

void MobileMapView::setProviderId(const QString &id)
{
    this->impl->model.setProviderId(id);
}

bool MobileMapView::wireframe() const
{
    return this->impl->view->terrainWireframeVisible();
}

void MobileMapView::setWireframe(bool enabled)
{
    if (wireframe() == enabled)
        return;
    this->impl->view->setTerrainWireframeVisible(enabled);
    emit terrainOptionsChanged();
}

bool MobileMapView::hillshade() const
{
    return this->impl->view->terrainHillshadeVisible();
}

void MobileMapView::setHillshade(bool enabled)
{
    if (hillshade() == enabled)
        return;
    this->impl->view->setTerrainHillshadeVisible(enabled);
    emit terrainOptionsChanged();
}

void MobileMapView::setGlobe(bool globe_mode)
{
    if (globe_mode && !this->impl->view->globeAvailable())
        return;
    if (!globe_mode)
    {
        setLookAround(false);
        this->impl->north_start = 0;
    }
    this->impl->model.setViewMode(globe_mode ? MapViewMode::Globe : MapViewMode::TwoD,
                                  this->impl->viewport());
}

bool MobileMapView::lookAround() const
{
    return this->impl->model.globeEyePose().has_value();
}

void MobileMapView::setLookAround(bool enabled)
{
    if (enabled == lookAround() || (enabled && !globe()))
        return;
    this->impl->resetGestures();
    this->impl->resetLookAround();
    MapModel &model = this->impl->model;
    if (enabled)
    {
        MapGlobeEyePose pose;
        pose.eye = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(model.centerLon(), model.centerLat(),
            model.viewGlobeYawDeg(), model.viewGlobePitchDeg(), model.viewGlobeDistanceM(),
            model.viewGlobeVerticalOffsetM(), model.viewGlobeCameraCollisionLiftM());
        double lon = 0.0;
        double lat = 0.0;
        if (!GeoWgs84Ellipsoid::ecefToGeodetic(pose.eye, &lon, &lat))
            return;
        const GeoWgs84Ellipsoid::LocalFrame frame = GeoWgs84Ellipsoid::localFrameAtGeodetic(lon, lat, 0.0);
        this->impl->eye_enu_to_ecef = QQuaternion::fromAxes(frame.east, frame.north, frame.up);
        // Keep the current view until a fresh sensor orientation arrives.
        const GeoWgs84Ellipsoid::OrbitCameraBasisRelative orbit =
            GeoWgs84Ellipsoid::orbitCameraBasisRelativeToOrigin(model.centerLon(), model.centerLat(),
                model.viewGlobeYawDeg(), model.viewGlobePitchDeg(), model.viewGlobeDistanceM(),
                model.viewGlobeVerticalOffsetM(), pose.eye, model.viewGlobeCameraCollisionLiftM());
        pose.orientation = QQuaternion::fromAxes(orbit.right, orbit.up, -orbit.forward);
        model.setGlobeEyePose(pose);
    }
    else
    {
        setLookAroundPanorama(false);
        model.setGlobeEyePose(std::nullopt);
        this->impl->returnToFix();
    }
    // Camera stabilization runs only during Look around.
    this->impl->updateCamera();
    // Raw sensor recording (tools/pull_look_around.sh) during Look around.
    if (lookAround())
        AowisMobileMap::LookAroundRecorder::instance().start();
    else
        AowisMobileMap::LookAroundRecorder::instance().stop();
    emit lookAroundChanged();
}

bool MobileMapView::lookAroundPanorama() const
{
    return this->impl->look_around_panorama;
}

void MobileMapView::setLookAroundPanorama(bool enabled)
{
    if (enabled == this->impl->look_around_panorama || (enabled && !lookAround()))
        return;
    QImage image;
    if (enabled)
    {
        image.load(QStringLiteral(":/aowis/panorama/look-around-test.jpg"));
        if (image.isNull())
        {
            qWarning() << "AOWIS look around: test panorama could not be loaded";
            return;
        }
    }
    this->impl->look_around_panorama = enabled;
    this->impl->view->setPanorama(image);
    emit lookAroundPanoramaChanged();
}

void MobileMapView::setDevicePose(const QQuaternion &pose, double pose_seconds,
                                  const QQuaternion &relative_pose, double relative_seconds,
                                  int screen_rotation_deg, double declination_deg)
{
    if (!lookAround() || !this->impl->application_active || pose.isNull())
        return;
    // Screen-top in native device coordinates is (sin(rotation), cos(rotation), 0).
    this->impl->screen_to_device = QQuaternion::fromAxisAndAngle(0, 0, 1, -screen_rotation_deg);
    // East-positive declination turns magnetic north toward east. ENU's
    // positive rotation about Up turns north toward west.
    this->impl->magnetic_to_true = std::isfinite(declination_deg)
        ? QQuaternion::fromAxisAndAngle(0, 0, 1, -declination_deg) : QQuaternion();
    // Inputs keep their sensor timestamps; the engine core advances in fixed
    // steps of sensor time and frames read it at their own time. Arrival and
    // frame times share sensorClockSeconds(), so any offset to the sensor
    // clock cancels.
    AowisMobileMap::LookAroundEngine &engine = this->impl->look_around_engine;
    const double arrival = sensorClockSeconds();
    engine.setMagnification(this->impl->model.globeEyePose()->magnification);
    // Own field orientation drives the view (setFieldPose()): the phone's
    // rotation vectors only set screen rotation and declination above.
    // Without field samples for 0.5 s the phone's sensors drive it instead,
    // so the view never freezes.
    if (this->impl->fieldDriven())
    {
        if (this->impl->look_around_field_started < 0.0)
            this->impl->look_around_field_started = arrival;
        const double since = this->impl->look_around_field_last >= 0.0
            ? arrival - this->impl->look_around_field_last : arrival - this->impl->look_around_field_started;
        const bool fallback = since > 0.5;
        this->impl->updateFieldStatus(arrival, fallback);
        if (!fallback)
            return;
    }
    if (!relative_pose.isNull() && std::isfinite(relative_seconds) && relative_seconds > 0.0)
    {
        if (relative_seconds != this->impl->look_around_last_gyro)
            ++this->impl->look_around_gyro_samples;
        this->impl->look_around_last_gyro = relative_seconds;
        engine.addGyro({relative_pose.x(), relative_pose.y(), relative_pose.z(), relative_pose.scalar()},
                       relative_seconds, arrival);
    }
    if (std::isfinite(pose_seconds) && pose_seconds > 0.0)
        engine.addCompass({pose.x(), pose.y(), pose.z(), pose.scalar()}, pose_seconds, arrival);
    if (this->impl->window != nullptr)
        this->impl->window->update();
}

QString MobileMapView::lookAroundFieldStatus() const
{
    return this->impl->look_around_field_status;
}

bool MobileMapView::lookAroundVision() const
{
    return this->impl->look_around_vision;
}

void MobileMapView::setLookAroundVision(bool enabled)
{
    if (enabled == this->impl->look_around_vision)
        return;
    this->impl->resetLookAround();
    this->impl->look_around_vision = enabled;
    this->impl->configureEngine();
    emit lookAroundVisionChanged();
    if (!enabled)
    {
        this->impl->updateCamera();
        this->impl->setVisionStatus(QString());
        return;
    }
#ifdef Q_OS_ANDROID
    const QCameraPermission permission;
    if (qApp->checkPermission(permission) != Qt::PermissionStatus::Granted)
    {
        this->impl->look_around_vision_permission_pending = true;
        this->impl->setVisionStatus(QStringLiteral("Camera: waiting for permission"));
        qApp->requestPermission(permission, this, [this](const QPermission &result) {
            this->impl->look_around_vision_permission_pending = false;
            if (result.status() != Qt::PermissionStatus::Granted)
            {
                this->impl->setVisionStatus(QStringLiteral("Camera: permission denied - compass fallback"));
                return;
            }
            this->impl->updateCamera();
        });
        return;
    }
#endif
    this->impl->updateCamera();
}

QString MobileMapView::lookAroundVisionStatus() const
{
    return this->impl->look_around_vision_status;
}

void MobileMapView::Impl::configureEngine()
{
    // Camera: the fusion always supplies motion and the compass only
    // corrects north slowly. Otherwise heading from the compass while turning.
    this->look_around_engine.setHeadingFromCompass(!this->look_around_vision);
    // Field and camera motion already point north: show them at once.
    this->look_around_engine.setMotionIsGeographic(fieldDriven());
    this->look_around_engine.setMotionCorrectionFraction(this->look_around_vision
        ? AowisMobileMap::VisionFusion::kEngineMotionCorrectionFraction
        : AowisMobileMap::LookAroundEngine::kMotionCorrectionFraction);
    this->look_around_engine.setGyroTimeoutSeconds(this->look_around_vision
        ? AowisMobileMap::VisionFusion::kEngineGyroTimeoutSeconds
        : AowisMobileMap::LookAroundEngine::kGyroTimeoutSeconds);
}

void MobileMapView::Impl::setVisionStatus(const QString &status)
{
    if (status == this->look_around_vision_status)
        return;
    this->look_around_vision_status = status;
    emit this->item->lookAroundVisionStatusChanged();
}

// Runs the camera exactly while camera stabilization is on, Look around is
// on, the app is in the foreground and the permission is granted.
void MobileMapView::Impl::updateCamera()
{
    const bool wanted = this->look_around_vision && this->application_active &&
                        this->model.globeEyePose().has_value() && !this->look_around_vision_permission_pending;
    if (!wanted)
    {
        if (this->look_around_camera)
            this->look_around_camera->stop();
        return;
    }
    if (!this->look_around_camera)
    {
        this->look_around_camera = std::make_unique<LookAroundCamera>();
        this->look_around_camera->setResultHandler([this](const AowisMobileMap::VisionTracker::Result &result) {
            handleVision(result);
        });
    }
    if (this->look_around_camera->active())
        return;
#ifdef Q_OS_ANDROID
    if (qApp->checkPermission(QCameraPermission()) != Qt::PermissionStatus::Granted)
    {
        setVisionStatus(QStringLiteral("Camera: permission denied - compass fallback"));
        return;
    }
#endif
    this->look_around_fusion.reset();
    this->look_around_vision_frames = 0;
    this->look_around_vision_valid = 0;
    this->look_around_vision_status_time = -1.0;
    if (!this->look_around_camera->start())
        setVisionStatus(QStringLiteral("Camera: %1 - compass fallback").arg(this->look_around_camera->problem()));
    else
        setVisionStatus(QStringLiteral("Camera: starting"));
}

void MobileMapView::Impl::handleVision(const AowisMobileMap::VisionTracker::Result &result)
{
    if (!this->look_around_vision || !this->application_active)
        return;
    AowisMobileMap::LookAroundRecorder::instance().add('C', result.toSeconds,
        {result.valid ? 1.0 : 0.0, double(result.inliers), result.rotation.x, result.rotation.y,
         result.rotation.z, result.rotation.w});
    ++this->look_around_vision_frames;
    if (result.valid)
    {
        ++this->look_around_vision_valid;
        this->look_around_vision_points = result.inliers;
    }
    const double arrival = sensorClockSeconds();
    // The camera only turns the fused pose; it reaches the engine with the
    // next field step (motion sensors' time line).
    this->look_around_fusion.addVision(result, arrival);
    if (this->look_around_vision_status_time < 0.0)
        this->look_around_vision_status_time = arrival;
    const double span = arrival - this->look_around_vision_status_time;
    if (span < 0.5)
        return;
    const double fps = this->look_around_vision_frames / span;
    const double reliable = this->look_around_vision_frames > 0
        ? 100.0 * this->look_around_vision_valid / this->look_around_vision_frames : 0.0;
    QString state;
    if (this->look_around_fusion.orientation() < 0)
        state = QStringLiteral("learning image orientation - move the phone");
    else if (this->look_around_fusion.visionActive(arrival))
        state = QStringLiteral("tracking, %1 points").arg(this->look_around_vision_points);
    else
        state = QStringLiteral("nothing reliable - compass fallback");
    setVisionStatus(QStringLiteral("Camera: %1 fps, %2 % reliable\n%3")
                        .arg(fps, 0, 'f', 0).arg(reliable, 0, 'f', 0).arg(state));
    this->look_around_vision_frames = 0;
    this->look_around_vision_valid = 0;
    this->look_around_vision_status_time = arrival;
}

void MobileMapView::Impl::updateFieldStatus(double now, bool fallback)
{
    if (this->look_around_field_status_time >= 0.0 && now - this->look_around_field_status_time < 0.5)
        return;
    const double span = this->look_around_field_status_time >= 0.0 ? now - this->look_around_field_status_time : 0.0;
    const double rate = span > 0.0 ? this->look_around_field_count / span : 0.0;
    this->look_around_field_status_time = now;
    this->look_around_field_count = 0;
    QString status;
    if (fallback)
    {
        const double silent = this->look_around_field_last >= 0.0 ? now - this->look_around_field_last
                                                                   : now - this->look_around_field_started;
        status = QStringLiteral("Field: no samples for %1 s\n%6 calls%7\n%2 offered, %3 rejected\n"
                                "newest %4 s, accepted %5 s\nphone sensors drive the view")
            .arg(silent, 0, 'f', 1)
            .arg(this->look_around_field_calls)
            .arg(this->look_around_field_rejected)
            .arg(this->look_around_field_offered, 0, 'f', 2)
            .arg(this->look_around_field_accepted, 0, 'f', 2)
            .arg(this->look_around_field_entries)
            .arg(this->look_around_field_guard.isEmpty() ? QString()
                                                         : QStringLiteral(" refused: ") + this->look_around_field_guard);
        this->look_around_field_entries = 0;
        this->look_around_field_calls = 0;
        this->look_around_field_rejected = 0;
    }
    else
        status = QStringLiteral("Field: %1 Hz, %2 uT\ndip %3 deg, %4")
            .arg(rate, 0, 'f', 0)
            .arg(this->look_around_field_strength, 0, 'f', 1)
            .arg(this->look_around_field_dip, 0, 'f', 1)
            .arg(this->look_around_field.trusted() ? QStringLiteral("trusted")
                                                   : QStringLiteral("disturbed, heading held"));
    if (status != this->look_around_field_status)
    {
        this->look_around_field_status = status;
        emit this->item->lookAroundFieldStatusChanged();
    }
}

void MobileMapView::setFieldPose(const QQuaternion &pose, double seconds, double strength,
                                 double dip_deg, double expected_strength, double expected_dip_deg)
{
    ++this->impl->look_around_field_entries;
    const char *guard = !lookAround() ? "no look around"
                      : !this->impl->application_active ? "app inactive"
                      : !this->impl->fieldDriven() ? "mode off"
                      : pose.isNull() ? "null pose"
                      : !std::isfinite(seconds) || seconds <= 0.0 ? "bad time" : nullptr;
    this->impl->look_around_field_guard = guard != nullptr ? QString::fromLatin1(guard) : QString();
    if (guard != nullptr)
        return;
    AowisMobileMap::LookAroundFieldSource &field = this->impl->look_around_field;
    field.setExpectedField(expected_strength, expected_dip_deg);
    AowisMobileMap::LookAroundFieldSource::Step step;
    ++this->impl->look_around_field_calls;
    this->impl->look_around_field_offered = seconds;
    if (!field.add({pose.x(), pose.y(), pose.z(), pose.scalar()}, seconds, strength, dip_deg, &step))
    {
        // Repeats of the same reading arrive with every compass sample;
        // only count a reading that is really older.
        if (seconds < this->impl->look_around_field_accepted)
            ++this->impl->look_around_field_rejected;
        return;
    }
    this->impl->look_around_field_accepted = seconds;
    const double arrival = sensorClockSeconds();
    if (this->impl->look_around_field_last < 0.0 ||
        arrival - this->impl->look_around_field_last > 0.5)
    {
        // First field sample, or back after the fallback: the engine starts
        // over on this input (the phone's frame differs).
        this->impl->look_around_engine.reset();
        this->impl->look_around_fusion.reset();
    }
    this->impl->look_around_field_last = arrival;
    ++this->impl->look_around_field_count;
    this->impl->look_around_field_strength = strength;
    this->impl->look_around_field_dip = dip_deg;
    this->impl->updateFieldStatus(arrival, false);
    // Motion (heading gated to turns) moves the view; the field orientation
    // corrects north only while trusted (turn-gated in the engine).
    AowisMobileMap::LookAroundEngine &engine = this->impl->look_around_engine;
    engine.setMagnification(this->impl->model.globeEyePose()->magnification);
    // The fusion moves the pose (camera heading, else the field's own
    // motion); field readings are dated back by their filter delay.
    AowisMobileMap::VisionFusion::Output fused;
    if (this->impl->look_around_fusion.addField(step, arrival, &fused))
        engine.addGyro(fused.motion, fused.seconds, arrival);
    if (step.trusted)
        engine.addCompass(step.compass, AowisMobileMap::VisionFusion::fieldTime(step.seconds), arrival);
    ++this->impl->look_around_gyro_samples;
    if (this->impl->window != nullptr)
        this->impl->window->update();
}

void MobileMapView::Impl::applyLookAroundFrame()
{
    if (!this->application_active || !this->look_around_engine.hasPose() ||
        !this->model.globeEyePose().has_value() || !mapViewVisible())
        return;
    MapGlobeEyePose camera = *this->model.globeEyePose();
    // The screen shows frames one refresh apart, but the moment a frame is
    // prepared jitters by several ms. Sample the motion at the phase-locked
    // presentation time, not at that raw moment.
    const double wall = sensorClockSeconds();
    const double paced = this->look_around_clock.frameTime(wall);
    const AowisCompass::Quaternion shown = this->look_around_engine.poseForFrame(paced);
    const QQuaternion device_pose(float(shown.w), float(shown.x),
                                  float(shown.y), float(shown.z));
    if (this->look_around_frames.size() < kLookAroundRecordFrames)
    {
        const QVector3D axis = device_pose.rotatedVector(QVector3D(0.0f, 0.0f, -1.0f));
        LookAroundFrame record;
        record.wall = wall;
        record.paced = paced;
        record.core = this->look_around_engine.coreSeconds();
        record.delay = this->look_around_engine.displayDelaySeconds();
        record.bearing = std::atan2(double(axis.x()), double(axis.y())) * 180.0 / std::acos(-1.0);
        record.pitch = std::asin(std::clamp(double(axis.z()), -1.0, 1.0)) * 180.0 / std::acos(-1.0);
        record.heading_noise = this->look_around_engine.headingNoiseDegrees();
        record.heading_correction = this->look_around_engine.headingCorrectionDegrees();
        record.uses_gyro = this->look_around_engine.usesGyro();
        record.mode_switches = this->look_around_engine.modeSwitches();
        record.field_motion = this->fieldDriven();
        record.field_trusted = this->look_around_field.trusted();
        record.gyro_samples = this->look_around_gyro_samples;
        record.panorama = this->look_around_panorama;
        this->look_around_frames.push_back(record);
    }
    this->look_around_gyro_samples = 0;
    const QQuaternion orientation = (this->eye_enu_to_ecef * this->magnetic_to_true *
                                     device_pose * this->screen_to_device).normalized();
    if (orientation == camera.orientation)
        return;
    camera.orientation = orientation;
    this->model.setGlobeEyePose(camera);
    // The shown moment moves on between sensor batches: keep frames coming
    // while the view is still changing.
    if (this->window != nullptr)
        this->window->update();
}

void MobileMapView::Impl::resetLookAround()
{
    writeLookAroundRecording();
    this->look_around_engine.reset();
    this->look_around_clock.reset();
    this->look_around_field.reset();
    this->look_around_fusion.reset();
    this->look_around_field_last = -1.0;
    this->look_around_field_started = -1.0;
    this->look_around_field_status_time = -1.0;
    this->look_around_field_count = 0;
    this->look_around_field_calls = 0;
    this->look_around_field_rejected = 0;
    this->look_around_gyro_samples = 0;
    this->look_around_last_gyro = 0.0;
}

void MobileMapView::Impl::writeLookAroundRecording()
{
    // Raw sensor events and camera results since the last write
    // (look_around_recorder.h; replayed by tools/replay_look_around.sh).
    const std::vector<AowisMobileMap::LookAroundRecorder::Record> records =
        AowisMobileMap::LookAroundRecorder::instance().take();
    if (records.size() > 100)
    {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/lookaround");
        QFile file(directory + QStringLiteral("/sensors-")
                   + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".csv"));
        if (QDir().mkpath(directory) && file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            QTextStream stream(&file);
            stream << "# AOWIS Mobile Look around raw sensors; seconds = sensor time, arrival = CLOCK_BOOTTIME\n"
                   << "# A accel m/s2 (x,y,z), M magnetometer uT (x,y,z), Y gyro rad/s (x,y,z),"
                      " R rotation vector / G game rotation vector (x,y,z,w),"
                      " U uncalibrated magnetometer uT (x,y,z, bias x,y,z),"
                      " C camera (valid,inliers,x,y,z,w; seconds = camera clock)\n"
                   << "# camera_stabilization=" << (this->look_around_vision ? 1 : 0)
                   << " camera_orientation=" << this->look_around_fusion.orientation()
                   << " camera_reported_orientation="
                   << (this->look_around_camera ? this->look_around_camera->reportedOrientation() : -1) << "\n"
                   << "kind,seconds,arrival,v0,v1,v2,v3,v4,v5\n";
            for (const AowisMobileMap::LookAroundRecorder::Record &record : records)
            {
                stream << record.kind << ',' << QString::number(record.seconds, 'f', 6) << ','
                       << QString::number(record.arrival, 'f', 6);
                for (double value : record.values)
                    stream << ',' << QString::number(value, 'g', 9);
                stream << '\n';
            }
            stream.flush();
            qInfo().noquote() << "AOWIS look around: recorded" << records.size() << "sensor events to" << file.fileName();
        }
    }
    if (this->look_around_frames.size() < 2)
    {
        this->look_around_frames.clear();
        return;
    }
    // Pull with: adb exec-out run-as org.aowis.mobile tar -C files/lookaround -cf - . | tar -xf -
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/lookaround");
    if (QDir().mkpath(directory))
    {
        QFile file(directory + QStringLiteral("/frames-")
                   + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".csv"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            QTextStream stream(&file);
            stream << "# AOWIS Mobile Look around frames; times in ms from the first frame (sensor clock)\n"
                   << "# core_lag_ms = frame time - newest core time (meaningful when the clocks agree)\n"
                   << "# heading_noise_deg: sample-to-sample heading noise of the driving input (gyroscope-grade: below 0.05)\n"
                   << "# source: 1 = game rotation vector drives the view, 0 = compass rotation vector\n"
                   << "# mode_switches: hand-overs between them so far (rising steadily: game rotation vector in bursts)\n"
                   << "# field_motion: 1 = own accelerometer + magnetometer orientation drives the view;"
                      " field_trusted: 0 = magnetometer disturbed, heading held\n"
                   << "wall_ms,interval_ms,paced_ms,paced_step_ms,core_lag_ms,delay_ms,bearing_deg,pitch_deg,"
                      "heading_noise_deg,heading_correction_deg,source,mode_switches,field_motion,field_trusted,"
                      "gyro_samples,panorama\n";
            const double origin = this->look_around_frames.front().wall;
            for (std::size_t index = 0; index < this->look_around_frames.size(); ++index)
            {
                const LookAroundFrame &frame = this->look_around_frames[index];
                const LookAroundFrame &previous = this->look_around_frames[index == 0 ? 0 : index - 1];
                stream << QString::number((frame.wall - origin) * 1e3, 'f', 3) << ','
                       << QString::number((frame.wall - previous.wall) * 1e3, 'f', 3) << ','
                       << QString::number((frame.paced - origin) * 1e3, 'f', 3) << ','
                       << QString::number((frame.paced - previous.paced) * 1e3, 'f', 3) << ','
                       << QString::number((frame.wall - frame.core) * 1e3, 'f', 3) << ','
                       << QString::number(frame.delay * 1e3, 'f', 3) << ','
                       << QString::number(frame.bearing, 'f', 4) << ','
                       << QString::number(frame.pitch, 'f', 4) << ','
                       << QString::number(frame.heading_noise, 'f', 4) << ','
                       << QString::number(frame.heading_correction, 'f', 4) << ','
                       << (frame.uses_gyro ? 1 : 0) << ','
                       << frame.mode_switches << ','
                       << (frame.field_motion ? 1 : 0) << ','
                       << (frame.field_trusted ? 1 : 0) << ','
                       << frame.gyro_samples << ','
                       << (frame.panorama ? 1 : 0) << '\n';
            }
            stream.flush();
            qInfo().noquote() << "AOWIS look around: recorded" << this->look_around_frames.size()
                              << "frames to" << file.fileName();
        }
    }
    this->look_around_frames.clear();
}

double MobileMapView::northAngle() const
{
    if (lookAround())
    {
        const QQuaternion screen_to_enu = this->impl->eye_enu_to_ecef.conjugated()
            * this->impl->model.globeEyePose()->orientation;
        const double bearing_deg = AowisCompass::headingFromRotationVector(
            screen_to_enu.x(), screen_to_enu.y(), screen_to_enu.z(), screen_to_enu.scalar(), 0);
        if (bearing_deg >= 0.0)
            return -bearing_deg;
        QVector3D bearing = screen_to_enu.rotatedVector(QVector3D(0, 0, -1));
        if (std::hypot(bearing.x(), bearing.y()) < 0.001)
            bearing = screen_to_enu.rotatedVector(QVector3D(0, 1, 0));
        return -std::atan2(bearing.x(), bearing.y()) * 180.0 / std::acos(-1.0);
    }
    // Render's positive yaw places local north clockwise from screen-up.
    return this->impl->model.viewGlobeYawDeg();
}

void MobileMapView::faceNorth()
{
    if (!globe())
        return;
    if (lookAround())
        setLookAround(false);
    this->impl->north_start_angle = std::remainder(northAngle(), 360.0);
    this->impl->pan_velocity = {};
    if (this->impl->north_start_angle == 0.0)
    {
        this->impl->north_start = 0;
        this->impl->model.setViewGlobeYawDeg(0.0);
        return;
    }
    this->impl->north_start = steadyMicroseconds();
    this->impl->startMotion();
}

void MobileMapView::zoomBy(double steps)
{
    const QSize size = this->impl->viewport();
    this->impl->zoom_anchor = QPointF(size.width() * 0.5, size.height() * 0.5);
    this->impl->zoom_remaining += steps;
    this->impl->startMotion();
}

void MobileMapView::reloadServer()
{
    this->impl->rebuildServer();
}

void MobileMapView::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange)
        this->impl->attachWindow(value.window);
    if (change == ItemVisibleHasChanged)
    {
        if (value.boolValue)
        {
            this->impl->view->handleHostShown();
            this->impl->returnToFix();
        }
        else
        {
            this->impl->view->handleHostHidden();
            this->impl->resetGestures();
        }
    }
    QQuickItem::itemChange(change, value);
}

void MobileMapView::geometryChange(const QRectF &new_geometry, const QRectF &old_geometry)
{
    QQuickItem::geometryChange(new_geometry, old_geometry);
    if (new_geometry.size() != old_geometry.size())
    {
        this->impl->view->handleHostResized();
        this->impl->view->requestRedraw();
    }
}

void MobileMapView::touchEvent(QTouchEvent *event)
{
    this->impl->touch(event);
    event->accept();
}

void MobileMapView::touchUngrabEvent()
{
    if (this->impl->touch_mode != Impl::Touch::None)
    {
        this->impl->resetGestures();
        this->impl->returnToFix();
    }
}

void MobileMapView::mousePressEvent(QMouseEvent *event)
{
    if (lookAround())
    {
        event->ignore();
        return;
    }
    const QPointF position = event->position();
    this->impl->stopMotion();
    this->impl->mouse_last = position;
    if (event->button() == Qt::RightButton && globe())
    {
        this->impl->mouse = Impl::Pointer::Orbit;
        this->impl->beginOrbit();
    }
    else
    {
        this->impl->mouse = Impl::Pointer::Pan;
        this->impl->pan_remainder = QPointF();
    }
    event->accept();
}

void MobileMapView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF position = event->position();
    const QPointF delta = position - this->impl->mouse_last;
    this->impl->mouse_last = position;
    switch (this->impl->mouse)
    {
        case Impl::Pointer::Pan:
            this->impl->sampleDrag(delta);
            this->impl->panBy(delta);
            break;
        case Impl::Pointer::Orbit:
            this->impl->model.orbitViewGlobeByPointerDelta(
                QPoint(qRound(delta.x()), qRound(delta.y())), true);
            break;
        case Impl::Pointer::None:
            break;
    }
    event->accept();
}

void MobileMapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->impl->mouse == Impl::Pointer::Orbit)
        this->impl->endOrbit();
    this->impl->mouse = Impl::Pointer::None;
    this->impl->releaseMotion();
    event->accept();
}

void MobileMapView::mouseUngrabEvent()
{
    if (this->impl->mouse != Impl::Pointer::None)
    {
        this->impl->resetGestures();
        this->impl->returnToFix();
    }
}

void MobileMapView::wheelEvent(QWheelEvent *event)
{
    const double notches = double(event->angleDelta().y()) / 120.0;
    this->impl->zoom_anchor = event->position();
    this->impl->zoom_remaining += notches * 0.5;
    this->impl->startMotion();
    event->accept();
}

#else // !AOWIS_MOBILE_WITH_MAP

// Built without the renderer: the QML API exists, nothing is drawn.
struct MobileMapView::Impl
{
    QPointer<QObject> map_settings;
    QPointer<QObject> wireguard;
    bool dark = false;
};

MobileMapView::MobileMapView(QQuickItem *parent)
    : QQuickItem(parent), impl(std::make_unique<Impl>())
{
}

MobileMapView::~MobileMapView() = default;

QObject *MobileMapView::wireguard() const { return this->impl->wireguard; }
void MobileMapView::setWireguard(QObject *settings)
{
    if (this->impl->wireguard == settings)
        return;
    this->impl->wireguard = settings;
    emit wireguardChanged();
}
QObject *MobileMapView::mapSettings() const { return this->impl->map_settings; }
void MobileMapView::setMapSettings(QObject *settings)
{
    if (this->impl->map_settings == settings) return;
    this->impl->map_settings = settings;
    emit mapSettingsChanged();
}
bool MobileMapView::serverBlocked() const { return false; }
bool MobileMapView::darkTheme() const { return this->impl->dark; }
void MobileMapView::setDarkTheme(bool dark)
{
    if (this->impl->dark == dark)
        return;
    this->impl->dark = dark;
    emit darkThemeChanged();
}
bool MobileMapView::rendererAvailable() const { return false; }
bool MobileMapView::savedCamera() const { return false; }
bool MobileMapView::globe() const { return false; }
double MobileMapView::centerLongitude() const { return 0.0; }
double MobileMapView::centerLatitude() const { return 0.0; }
QString MobileMapView::status() const { return tr("Built without the map renderer"); }
QString MobileMapView::serverStatus() const { return QString(); }
QString MobileMapView::diagnostics() const { return QString(); }
QString MobileMapView::lifecycle() const { return QString(); }
void MobileMapView::centerOn(double, double, double) {}
bool MobileMapView::globeAvailable() const { return false; }
QVariantList MobileMapView::providers() const { return {}; }
QString MobileMapView::providerId() const { return {}; }
void MobileMapView::setProviderId(const QString &) {}
bool MobileMapView::wireframe() const { return false; }
void MobileMapView::setWireframe(bool) {}
bool MobileMapView::hillshade() const { return false; }
void MobileMapView::setHillshade(bool) {}
void MobileMapView::setLocationMarker(bool, double, double, bool, bool) {}
void MobileMapView::setGlobe(bool) {}
bool MobileMapView::lookAround() const { return false; }
void MobileMapView::setLookAround(bool) {}
bool MobileMapView::lookAroundPanorama() const { return false; }
void MobileMapView::setLookAroundPanorama(bool) {}
void MobileMapView::setDevicePose(const QQuaternion &, double, const QQuaternion &, double, int, double) {}
void MobileMapView::setFieldPose(const QQuaternion &, double, double, double, double, double) {}
QString MobileMapView::lookAroundFieldStatus() const { return QString(); }
bool MobileMapView::lookAroundVision() const { return false; }
void MobileMapView::setLookAroundVision(bool) {}
QString MobileMapView::lookAroundVisionStatus() const { return QString(); }
double MobileMapView::northAngle() const { return 0.0; }
void MobileMapView::faceNorth() {}
void MobileMapView::zoomBy(double) {}
void MobileMapView::reloadServer() {}
void MobileMapView::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
}
void MobileMapView::geometryChange(const QRectF &new_geometry, const QRectF &old_geometry)
{
    QQuickItem::geometryChange(new_geometry, old_geometry);
}
void MobileMapView::touchEvent(QTouchEvent *event) { QQuickItem::touchEvent(event); }
void MobileMapView::touchUngrabEvent() {}
void MobileMapView::mousePressEvent(QMouseEvent *event) { QQuickItem::mousePressEvent(event); }
void MobileMapView::mouseMoveEvent(QMouseEvent *event) { QQuickItem::mouseMoveEvent(event); }
void MobileMapView::mouseReleaseEvent(QMouseEvent *event) { QQuickItem::mouseReleaseEvent(event); }
void MobileMapView::mouseUngrabEvent() {}
void MobileMapView::wheelEvent(QWheelEvent *event) { QQuickItem::wheelEvent(event); }

#endif
