#pragma once

#include <QQuickItem>
#include <QString>
#include <QVariantList>

#include <memory>

class QKeyEvent;
class QMouseEvent;
class QTouchEvent;
class QWheelEvent;

// The AOWIS map (AOWIS-GUI-Render MapBgfxMapView: 2D and globe, imagery and
// terrain) inside the Qt Quick scene. The map is drawn underneath the QML
// scene into Qt Quick's own OpenGL / OpenGL ES context (the view's host
// context mode): this item must fill the window and stay below the QML
// controls. Tiles, terrain and the imagery providers come from the
// aowis-server-map at serverUrl.
//
// Input: one finger (or the left mouse button) pans, two fingers pinch-zoom
// and, on the globe, turn (rotate) and tilt (move up/down); the wheel zooms,
// the right mouse button orbits the globe.
//
// Built without the renderer (AOWIS_MOBILE_WITH_BGFX=OFF) the same QML API
// exists and draws nothing.
// Not final: qmlRegisterType derives QQmlElement<MobileMapView> from it.
class MobileMapView : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    Q_PROPERTY(bool rendererAvailable READ rendererAvailable CONSTANT)
    // The last session's camera was restored (no automatic centring then).
    Q_PROPERTY(bool savedCamera READ savedCamera CONSTANT)
    Q_PROPERTY(bool globe READ globe NOTIFY cameraChanged)
    Q_PROPERTY(bool globeAvailable READ globeAvailable NOTIFY statusChanged)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QString providerId READ providerId WRITE setProviderId NOTIFY providerIdChanged)
    Q_PROPERTY(bool wireframe READ wireframe WRITE setWireframe NOTIFY terrainOptionsChanged)
    Q_PROPERTY(bool hillshade READ hillshade WRITE setHillshade NOTIFY terrainOptionsChanged)
    Q_PROPERTY(double centerLongitude READ centerLongitude NOTIFY cameraChanged)
    Q_PROPERTY(double centerLatitude READ centerLatitude NOTIFY cameraChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString serverStatus READ serverStatus NOTIFY statusChanged)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY statusChanged)
    Q_PROPERTY(QString lifecycle READ lifecycle NOTIFY statusChanged)

public:
    explicit MobileMapView(QQuickItem *parent = nullptr);
    ~MobileMapView() override;

    QString serverUrl() const;
    void setServerUrl(const QString &url);
    bool darkTheme() const;
    void setDarkTheme(bool dark);
    bool rendererAvailable() const;
    bool savedCamera() const;
    bool globe() const;
    bool globeAvailable() const;
    QVariantList providers() const;
    QString providerId() const;
    void setProviderId(const QString &id);
    bool wireframe() const;
    void setWireframe(bool enabled);
    bool hillshade() const;
    void setHillshade(bool enabled);
    double centerLongitude() const;
    double centerLatitude() const;
    QString status() const;
    QString serverStatus() const;
    QString diagnostics() const;
    QString lifecycle() const;

    // Camera: 2D at a Web Mercator zoom level, or the globe.
    Q_INVOKABLE void centerOn(double longitude_deg, double latitude_deg, double zoom);
    Q_INVOKABLE void setLocationMarker(bool valid, double longitude, double latitude, bool fresh, bool follow);
    Q_INVOKABLE void setGlobe(bool globe);
    Q_INVOKABLE void zoomBy(double steps);
    // Loads the map server's provider catalog and terrain datasets again.
    Q_INVOKABLE void reloadServer();

signals:
    void serverUrlChanged();
    void darkThemeChanged();
    void cameraChanged();
    void repeatedGpsReturn();
    void providersChanged();
    void providerIdChanged();
    void terrainOptionsChanged();
    void statusChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) override;
    void touchEvent(QTouchEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void touchUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
