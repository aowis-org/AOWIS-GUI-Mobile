#include "mobile_map_view.h"

#include <QCoreApplication>
#include <QCursor>

#if AOWIS_MOBILE_WITH_MAP

#include "lifecycle_watchdog.h"
#include "mobile_map_math.h"
#include "compass_heading.h"

#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/bgfx/map_bgfx_map_view.h"
#include "map/core/map_model.h"
#include "map/core/map_provider_catalog.h"
#include "map/core/map_view_types.h"
#include "map/data/map_server_transport.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_tile_repository.h"
#include "map/render/map_render_types.h"
#include "services/interface_server_map_rest.h"
#include "services/map_server_rest_configuration.h"

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
#include <utility>
#include <vector>

namespace
{
int64_t steadyMicroseconds()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
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
    void releaseContext();
    void rebuildServer();
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
    QString server_url;
    bool dark = false;
    MapProviderCatalog catalog;
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
    QObject::connect(&this->catalog, &MapProviderCatalog::catalogChanged, this->item, [this]()
    {
        this->model.providerCatalogChanged();
        emit this->item->providersChanged();
        if (this->tiles)
            this->tiles->invalidateProviderCatalog();
        this->server_status = MobileMapView::tr("Map server: %1 imagery providers").arg(this->catalog.providers().size());
        emit this->item->statusChanged();
    });
    QObject::connect(&this->catalog, &MapProviderCatalog::fallbackChanged, this->item, [this]()
    {
        this->model.providerFallbackChanged();
    });
    QObject::connect(&this->catalog, &MapProviderCatalog::errorOccurred, this->item,
                     [this](const QString &message)
    {
        this->server_status = MobileMapView::tr("Map server error: %1")
                                  .arg(message);
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
    this->server_status = MobileMapView::tr("No map server configured");
}

MobileMapView::Impl::~Impl()
{
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

void MobileMapView::Impl::rebuildServer()
{
    this->view->setTileRepository(nullptr);
    this->view->setTerrainRepository(nullptr);
    this->tiles.reset();
    this->terrain.reset();
    MapServerRestConfiguration server;
    server.base_url = this->server_url;
    this->catalog.setServer(server);
    emit this->item->providersChanged();
    if (!server.configured())
    {
        this->server_status = MobileMapView::tr("No map server configured");
        emit this->item->statusChanged();
        return;
    }
    MapServerTransport transport;
    transport.create = [server](QObject *parent) -> InterfaceServerMap *
    {
        return new InterfaceServerMapREST(server, parent);
    };
    this->tiles = std::make_unique<MapTileRepository>(transport);
    this->terrain = std::make_unique<MapTerrainRepository>(transport);
    this->view->setTileRepository(this->tiles.get());
    this->view->setTerrainRepository(this->terrain.get());
    this->server_status = MobileMapView::tr("Map server: connecting to %1")
                              .arg(this->server_url);
    emit this->item->statusChanged();
    this->catalog.refresh();
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
        beginOrbit();
        return;
    }
    const double zoom_steps = this->touch_distance > 1.0 && distance > 1.0
        ? AowisMobileMap::zoomStepsForScale(distance / this->touch_distance) : 0.0;
    this->zoom_anchor = centroid;
    zoomAt(zoom_steps, centroid);
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
}

MobileMapView::~MobileMapView() = default;

QString MobileMapView::serverUrl() const
{
    return this->impl->server_url;
}

void MobileMapView::setServerUrl(const QString &url)
{
    if (this->impl->server_url == url)
        return;
    this->impl->server_url = url;
    this->impl->rebuildServer();
    emit serverUrlChanged();
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
    if (!this->impl->catalog.server().configured())
        return result;
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
        model.setGlobeEyePose(std::nullopt);
        this->impl->returnToFix();
    }
    emit lookAroundChanged();
}

void MobileMapView::setDeviceOrientation(const QQuaternion &pose, int screen_rotation_deg, double declination_deg)
{
    if (!lookAround() || pose.isNull())
        return;
    MapGlobeEyePose camera = *this->impl->model.globeEyePose();
    // Screen-top in native device coordinates is (sin(rotation), cos(rotation), 0).
    const QQuaternion screen_to_device = QQuaternion::fromAxisAndAngle(0, 0, 1, -screen_rotation_deg);
    // East-positive declination turns magnetic north toward east. ENU's
    // positive rotation about Up turns north toward west.
    const QQuaternion magnetic_to_true = std::isfinite(declination_deg)
        ? QQuaternion::fromAxisAndAngle(0, 0, 1, -declination_deg) : QQuaternion();
    camera.orientation = (this->impl->eye_enu_to_ecef * magnetic_to_true * pose * screen_to_device).normalized();
    this->impl->model.setGlobeEyePose(camera);
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
    QString server_url;
    bool dark = false;
};

MobileMapView::MobileMapView(QQuickItem *parent)
    : QQuickItem(parent), impl(std::make_unique<Impl>())
{
}

MobileMapView::~MobileMapView() = default;

QString MobileMapView::serverUrl() const { return this->impl->server_url; }
void MobileMapView::setServerUrl(const QString &url)
{
    if (this->impl->server_url == url)
        return;
    this->impl->server_url = url;
    emit serverUrlChanged();
}
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
void MobileMapView::setDeviceOrientation(const QQuaternion &, int, double) {}
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
