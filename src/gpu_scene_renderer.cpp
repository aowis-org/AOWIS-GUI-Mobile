#include "gpu_scene_renderer.h"

#include <QByteArray>
#include <QFile>
#include <QDebug>
#include <QMatrix4x4>
#include <QString>

#include <cstdint>
#include <algorithm>
#include <limits>
#include <vector>

bgfx::ShaderHandle GpuSceneRenderer::loadShader(const char *name, const char *backend)
{
    const QString resource = QStringLiteral(":/aowis_lab/shaders/%1/%2.bin")
        .arg(QString::fromLatin1(backend), QString::fromLatin1(name));
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning().noquote() << "AOWIS renderer: missing shader" << resource;
        return BGFX_INVALID_HANDLE;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty() || data.size() > static_cast<qsizetype>(std::numeric_limits<uint32_t>::max()))
        return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.constData(), static_cast<uint32_t>(data.size())));
}

bgfx::VertexBufferHandle GpuSceneRenderer::createBuffer(
    const std::vector<AowisMobileGpu::Vertex> &vertices, const bgfx::VertexLayout &layout)
{
    if (vertices.empty() || vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(AowisMobileGpu::Vertex))
        return BGFX_INVALID_HANDLE;
    return bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(AowisMobileGpu::Vertex))), layout);
}

bool GpuSceneRenderer::initialize()
{
    const bgfx::RendererType::Enum backendType = bgfx::getRendererType();
    const char *backend = nullptr;
    if (backendType == bgfx::RendererType::OpenGL)
        backend = "glsl";
    else if (backendType == bgfx::RendererType::OpenGLES)
        backend = "essl";
    if (backend == nullptr)
    {
        qWarning("AOWIS renderer: network benchmark requires OpenGL or OpenGLES");
        return false;
    }

    const bgfx::ShaderHandle vertexShader = loadShader("vs_cubes", backend);
    if (!bgfx::isValid(vertexShader))
        return false;
    const bgfx::ShaderHandle fragmentShader = loadShader("fs_cubes", backend);
    if (!bgfx::isValid(fragmentShader))
    {
        bgfx::destroy(vertexShader);
        return false;
    }
    this->program = bgfx::createProgram(vertexShader, fragmentShader, true);
    if (!bgfx::isValid(this->program))
    {
        qWarning("AOWIS renderer: could not create vertex-color shader program");
        return false;
    }

    this->vertexLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    const AowisMobileGpu::Scene scene = AowisMobileGpu::createScene();
    this->minorGrid = createBuffer(scene.minorGridLines, this->vertexLayout);
    this->majorGrid = createBuffer(scene.majorGridLines, this->vertexLayout);
    this->pipes = createBuffer(scene.pipeTriangles, this->vertexLayout);
    this->nodes = createBuffer(scene.nodeTriangles, this->vertexLayout);
    if (!bgfx::isValid(this->minorGrid) || !bgfx::isValid(this->majorGrid) ||
        !bgfx::isValid(this->pipes) || !bgfx::isValid(this->nodes))
    {
        qWarning("AOWIS renderer: could not allocate baseline GPU geometry");
        this->release();
        return false;
    }
    this->baseVertexBytes = sizeof(AowisMobileGpu::Vertex) *
        (scene.minorGridLines.size() + scene.majorGridLines.size() +
         scene.pipeTriangles.size() + scene.nodeTriangles.size());
    return true;
}

void GpuSceneRenderer::submit(bgfx::VertexBufferHandle handle, uint64_t state)
{
    if (!bgfx::isValid(handle))
        return;
    bgfx::setVertexBuffer(0, handle);
    bgfx::setState(state);
    bgfx::submit(0, this->program);
}

void GpuSceneRenderer::destroyTile(CachedTile &tile)
{
    if (bgfx::isValid(tile.pipes)) bgfx::destroy(tile.pipes);
    if (bgfx::isValid(tile.nodes)) bgfx::destroy(tile.nodes);
    this->cachedVertexBytes -= tile.bytes;
    tile.pipes = BGFX_INVALID_HANDLE;
    tile.nodes = BGFX_INVALID_HANDLE;
    tile.bytes = 0;
}

void GpuSceneRenderer::clearTiles()
{
    for (CachedTile &tile : this->cachedTiles)
        this->destroyTile(tile);
    this->cachedTiles.clear();
    this->sequence = 0;
    this->cachedVertexBytes = 0;
}

GpuSceneRenderer::CachedTile *GpuSceneRenderer::findTile(const AowisMobileGpu::StressTile &key)
{
    for (CachedTile &tile : this->cachedTiles)
    {
        if (tile.key.column == key.column && tile.key.row == key.row &&
            tile.key.stride == key.stride)
            return &tile;
    }
    return nullptr;
}

GpuSceneRenderer::CachedTile *GpuSceneRenderer::loadTile(
    const AowisMobileGpu::StressProfile &profile, const AowisMobileGpu::StressTile &key)
{
    const AowisMobileGpu::StressMesh mesh = AowisMobileGpu::createStressTile(profile, key);
    if (mesh.nodes.empty())
        return nullptr;

    CachedTile tile;
    tile.key = key;
    tile.nodes = createBuffer(mesh.nodes, this->vertexLayout);
    tile.pipes = createBuffer(mesh.pipes, this->vertexLayout);
    if (!bgfx::isValid(tile.nodes) || !bgfx::isValid(tile.pipes))
    {
        if (bgfx::isValid(tile.nodes)) bgfx::destroy(tile.nodes);
        if (bgfx::isValid(tile.pipes)) bgfx::destroy(tile.pipes);
        qWarning("AOWIS renderer: GPU tile allocation failed");
        return nullptr;
    }
    tile.nodeCount = mesh.nodeCount;
    tile.pipeCount = mesh.pipeCount;
    tile.bytes = sizeof(AowisMobileGpu::Vertex) * (mesh.nodes.size() + mesh.pipes.size());
    tile.touched = this->sequence;

    // 96 tiles max, of which 64 may be visible. Bound both CPU geometry
    // preparation and resident vertex payloads even at the million-feature preset.
    if (this->cachedTiles.size() >= size_t(AowisMobileGpu::kMaximumCachedTiles))
    {
        std::vector<CachedTile>::iterator lru = std::min_element(
            this->cachedTiles.begin(), this->cachedTiles.end(),
            [](const CachedTile &first, const CachedTile &second) {
                return first.touched < second.touched;
            });
        this->destroyTile(*lru);
        this->cachedTiles.erase(lru);
    }
    this->cachedVertexBytes += tile.bytes;
    this->cachedTiles.push_back(tile);
    return &this->cachedTiles.back();
}

GpuSceneMetrics GpuSceneRenderer::render(int pixelWidth, int pixelHeight, double devicePixelRatio,
                                         double zoom, double panX, double panY, int workload)
{
    if (workload != this->activeWorkload)
    {
        this->clearTiles();
        this->activeWorkload = workload;
    }
    const AowisMobileGpu::Camera camera = AowisMobileGpu::makeCamera(
        pixelWidth, pixelHeight, devicePixelRatio, zoom, panX, panY);
    QMatrix4x4 projection;
    projection.ortho(camera.left, camera.right, camera.bottom, camera.top, -1.0f, 1.0f);

    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(std::min(pixelWidth, 65535)),
                      static_cast<uint16_t>(std::min(pixelHeight, 65535)));
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x102f45ff, 1.0f, 0);
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(0, nullptr, projection.constData());
    bgfx::touch(0);

    const uint64_t lines = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_LINES;
    const uint64_t triangles = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    this->submit(this->minorGrid, lines);
    this->submit(this->majorGrid, lines);

    GpuSceneMetrics result;
    if (workload == 0)
    {
        this->submit(this->pipes, triangles);
        this->submit(this->nodes, triangles);
        result.sampledNodes = 8;
        result.sampledPipes = 7;
    }
    else
    {
        const AowisMobileGpu::StressProfile profile = AowisMobileGpu::stressProfile(workload);
        const AowisMobileGpu::TileSelection selection =
            AowisMobileGpu::visibleStressTiles(camera, profile);
        result.visibleTiles = uint32_t(selection.tiles.size());
        result.stride = uint32_t(selection.stride);
        ++this->sequence;
        for (const AowisMobileGpu::StressTile &key : selection.tiles)
        {
            CachedTile *tile = this->findTile(key);
            if (tile == nullptr)
            {
                tile = this->loadTile(profile, key);
                if (tile != nullptr)
                    ++result.uploadsThisFrame;
            }
            if (tile != nullptr)
            {
                tile->touched = this->sequence;
                this->submit(tile->pipes, triangles);
                this->submit(tile->nodes, triangles);
                result.sampledNodes += tile->nodeCount;
                result.sampledPipes += tile->pipeCount;
            }
        }
    }
    result.residentTiles = uint32_t(this->cachedTiles.size());
    result.gpuVertexBytes = this->baseVertexBytes + this->cachedVertexBytes;
    return result;
}

void GpuSceneRenderer::release()
{
    this->clearTiles();
    if (bgfx::isValid(this->minorGrid)) bgfx::destroy(this->minorGrid);
    if (bgfx::isValid(this->majorGrid)) bgfx::destroy(this->majorGrid);
    if (bgfx::isValid(this->pipes)) bgfx::destroy(this->pipes);
    if (bgfx::isValid(this->nodes)) bgfx::destroy(this->nodes);
    if (bgfx::isValid(this->program)) bgfx::destroy(this->program);
    this->minorGrid = BGFX_INVALID_HANDLE;
    this->majorGrid = BGFX_INVALID_HANDLE;
    this->pipes = BGFX_INVALID_HANDLE;
    this->nodes = BGFX_INVALID_HANDLE;
    this->program = BGFX_INVALID_HANDLE;
    this->baseVertexBytes = 0;
}
