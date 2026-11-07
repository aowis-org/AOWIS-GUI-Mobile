# The map

The Map page shows the AOWIS map shared with the desktop GUI: Render's
`MapBgfxMapView` (AOWIS-GUI-Render) with 2D Web Mercator imagery, the 3D globe
with terrain. Mobile has no map code of its own beyond the
Qt Quick host and its gestures.

## How it is drawn

`MobileMapView` (`src/mobile_map_view.*`, QML type `AOWIS.Map 1.0` `MapView`)
is a `QQuickItem` that fills the window underneath all QML controls. It hosts
the view in `RenderTarget::HostContext`:

1. `src/main.cpp` selects Qt Quick's OpenGL scene graph (OpenGL ES on
   Android), the **basic render loop** and, on X11, EGL. The map's frame works
   on the map model, the tile/terrain repositories and their signals, which
   live on the GUI thread; the basic loop renders on that thread too.
2. On `QQuickWindow::beforeRenderPassRecording` the item takes Qt's current
   EGL context and calls `MapBgfxMapView::renderHostFrame()` inside
   `beginExternalCommands()` / `endExternalCommands()`. The first call
   initializes bgfx in that context (no window handle, no swapchain,
   `BGFX_RESET_NONE`).
3. Every Qt Quick frame draws the whole map (Qt cleared the framebuffer);
   frames are executed with `bgfx::frame(BGFX_FRAME_FLUSH)`, Qt presents.
4. The view asks for frames through `hostFrameRequested()` (camera changes,
   arriving tiles, animations); the item calls `QQuickWindow::update()`.
   An idle map renders nothing.
5. On `sceneGraphInvalidated` (Android backgrounding: persistent scene graph
   and graphics are off) the item calls `releaseHostContext()`; the next
   render pass initializes again. CPU tile and terrain caches survive.

bgfx, bx and bimg are Render's pinned submodules (the revisions the desktop
GUI builds), so Render's committed shader binaries match. Linux embeds the
GLSL shaders, Android the ESSL ones.

## Map server

`serverUrl` is bound to `mapServerSettings.endpointUrl` (Settings → Map
server). For a configured server the item creates Render's
`InterfaceServerMapREST` transport, the `MapTileRepository` and
`MapTerrainRepository`, and refreshes the server's imagery provider catalog
(`MapProviderCatalog`, `GET /map/v1/providers`). Changing the server rebuilds
them. Without a server the map shows its background and a banner points to
Settings. Diagnostics → **Reload map server** reconnects.

## Input

| Input | 2D | Globe |
| --- | --- | --- |
| One finger / left mouse drag | Pan | Pan the globe |
| Two-finger pinch / wheel | Zoom at the fingers / pointer | Zoom |
| Two-finger twist | — | Turn (heading) |
| Two fingers up / down | Pan | Tilt |
| Right mouse drag | — | Orbit |

Tap the folded-map icon at the top left to select an enabled imagery
provider, switch between 2D and 3D, or zoom. In 3D the same menu offers
wireframe and hillshade. Drags and
pinches elsewhere go directly to map navigation. Mobile disables the
renderer HUD, including its crosshair, scale bar and device-position marker.

The camera is saved when the app goes to the background and restored on the
next start. Without a saved camera the map opens on Central Europe. Enable
**Keep GPS marker centered** in the map menu to follow fresh fixes at the
current zoom. The location button also re-centres the map on demand.

## Lifecycle diagnostics

The item writes `AppDataLocation/lifecycle/events.csv` (app active/inactive,
scene graph released, bgfx initialized, `watch_<reason>`, `recovered`,
`late_recovery`, `timeout`) and arms a five-second first-frame watchdog after
resume, scene-graph recreation and resize. `tools/test_android_lifecycle.sh`
reads that log via `adb run-as`. Diagnostics shows the renderer, server and
recovery state.

## Limits

- No hydraulic network on the mobile map yet (`setDistributionNetworkVisible(false)`).
- Linux and Android only (EGL context borrowing).
- Gesture directions on the globe are first guesses and need device tuning.
