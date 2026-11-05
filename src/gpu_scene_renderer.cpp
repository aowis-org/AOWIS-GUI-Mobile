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
        qWarning().noquote() << "AOWIS render lab: missing shader" << resource;
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
        qWarning("AOWIS render lab: GPU geometry experiment requires OpenGL or OpenGLES");
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
        qWarning("AOWIS render lab: could not create vertex-color shader program");
        return false;
    }

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    const AowisMobileGpu::Scene scene = AowisMobileGpu::createScene();
    this->minorGrid = createBuffer(scene.minorGridLines, layout);
    this->majorGrid = createBuffer(scene.majorGridLines, layout);
    this->pipes = createBuffer(scene.pipeTriangles, layout);
    this->nodes = createBuffer(scene.nodeTriangles, layout);
    if (!bgfx::isValid(this->minorGrid) || !bgfx::isValid(this->majorGrid) ||
        !bgfx::isValid(this->pipes) || !bgfx::isValid(this->nodes))
    {
        qWarning("AOWIS render lab: could not allocate GPU geometry buffers");
        this->release();
        return false;
    }
    return true;
}

void GpuSceneRenderer::submit(bgfx::VertexBufferHandle handle, uint64_t state)
{
    bgfx::setVertexBuffer(0, handle);
    bgfx::setState(state);
    bgfx::submit(0, this->program);
}

void GpuSceneRenderer::render(int pixelWidth, int pixelHeight, double devicePixelRatio,
                              double zoom, double panX, double panY)
{
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

    // No CPU vertex rebuilding on zoom/pan. Each call uses a retained GPU buffer.
    const uint64_t lines = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_LINES;
    const uint64_t triangles = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    this->submit(this->minorGrid, lines);
    this->submit(this->majorGrid, lines);
    this->submit(this->pipes, triangles);
    this->submit(this->nodes, triangles);
}

void GpuSceneRenderer::release()
{
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
}
