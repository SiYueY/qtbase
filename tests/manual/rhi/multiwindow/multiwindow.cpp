// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>

#include <QCommandLineParser>
#include <QWindow>
#include <QPlatformSurfaceEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QOffscreenSurface>
#include <rhi/qrhi.h>

enum GraphicsApi
{
    OpenGL,
    Vulkan,
    D3D11,
    D3D12,
    Metal
};

static GraphicsApi graphicsApi;

static QString graphicsApiName()
{
    switch (graphicsApi) {
    case OpenGL:
        return QLatin1String("OpenGL 2.x");
    case Vulkan:
        return QLatin1String("Vulkan");
    case D3D11:
        return QLatin1String("Direct3D 11");
    case D3D12:
        return QLatin1String("Direct3D 12");
    case Metal:
        return QLatin1String("Metal");
    default:
        break;
    }
    return QString();
}

static struct {
#if QT_CONFIG(vulkan)
    QVulkanInstance *instance = nullptr;
#endif
    QRhi *r = nullptr;
#ifndef QT_NO_OPENGL
    QOffscreenSurface *fallbackSurface = nullptr;
#endif
} r;

void createRhi()
{
#ifndef QT_NO_OPENGL
    if (graphicsApi == OpenGL) {
        r.fallbackSurface = QRhiGles2InitParams::newFallbackSurface();
        QRhiGles2InitParams params;
        params.fallbackSurface = r.fallbackSurface;
        //params.window = this;
        r.r = QRhi::create(QRhi::OpenGLES2, &params);
    }
#endif

#if QT_CONFIG(vulkan)
    if (graphicsApi == Vulkan) {
        QRhiVulkanInitParams params;
        params.inst = r.instance;
        //params.window = this;
        r.r = QRhi::create(QRhi::Vulkan, &params);
    }
#endif

#ifdef Q_OS_WIN
    if (graphicsApi == D3D11) {
        QRhiD3D11InitParams params;
        params.enableDebugLayer = true;
        r.r = QRhi::create(QRhi::D3D11, &params);
    } else if (graphicsApi == D3D12) {
        QRhiD3D12InitParams params;
        params.enableDebugLayer = true;
        r.r = QRhi::create(QRhi::D3D12, &params);
    }
#endif

#if QT_CONFIG(metal)
    if (graphicsApi == Metal) {
        QRhiMetalInitParams params;
        r.r = QRhi::create(QRhi::Metal, &params);
    }
#endif

    if (!r.r)
        qFatal("Failed to create RHI backend");
}

void destroyRhi()
{
    delete r.r;

#ifndef QT_NO_OPENGL
    delete r.fallbackSurface;
#endif
}

struct {
    QList<QWindow *> windows;

    QRhiBuffer *vbuf = nullptr;
    QRhiBuffer *ubuf = nullptr;
    QRhiShaderResourceBindings *srb = nullptr;
    QRhiGraphicsPipeline *ps = nullptr;
    QRhiResourceUpdateBatch *initialUpdates = nullptr;
} d;

static float vertexData[] = {
     0.0f,   0.5f,   1.0f, 0.0f, 0.0f,
    -0.5f,  -0.5f,   0.0f, 1.0f, 0.0f,
     0.5f,  -0.5f,   0.0f, 0.0f, 1.0f,
};

static QShader getShader(const QString &name)
{
    QFile f(name);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());

    return QShader();
}

// can use just one rpd from whichever window comes first since they are
// actually compatible due to all windows using the same config (have
// depth-stencil, sample count 1, same format). this means the same pso can be
// reused too.
void ensureSharedResources(QRhiRenderPassDescriptor *rp)
{
    if (!d.vbuf) {
        d.vbuf = r.r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertexData));
        d.vbuf->create();
        d.initialUpdates = r.r->nextResourceUpdateBatch();
        d.initialUpdates->uploadStaticBuffer(d.vbuf, vertexData);
    }

    if (!d.ubuf) {
        d.ubuf = r.r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 68);
        d.ubuf->create();
    }

    if (!d.srb) {
        d.srb = r.r->newShaderResourceBindings();
        d.srb->setBindings({
                               QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, d.ubuf)
                           });
        d.srb->create();
    }

    if (!d.ps) {
        d.ps = r.r->newGraphicsPipeline();

        QRhiGraphicsPipeline::TargetBlend premulAlphaBlend;
        premulAlphaBlend.enable = true;
        d.ps->setTargetBlends({ premulAlphaBlend });

        const QShader vs = getShader(QLatin1String(":/color.vert.qsb"));
        if (!vs.isValid())
            qFatal("Failed to load shader pack (vertex)");
        const QShader fs = getShader(QLatin1String(":/color.frag.qsb"));
        if (!fs.isValid())
            qFatal("Failed to load shader pack (fragment)");

        d.ps->setShaderStages({
                                  { QRhiShaderStage::Vertex, vs },
                                  { QRhiShaderStage::Fragment, fs }
                              });

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({
            { 5 * sizeof(float) }
        });
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float3, 2 * sizeof(float) }
        });

        d.ps->setVertexInputLayout(inputLayout);
        d.ps->setShaderResourceBindings(d.srb);
        d.ps->setRenderPassDescriptor(rp);

        d.ps->create();
    }
}

void destroySharedResources()
{
    delete d.ps;
    d.ps = nullptr;

    delete d.srb;
    d.srb = nullptr;

    delete d.vbuf;
    d.vbuf = nullptr;

    delete d.ubuf;
    d.ubuf = nullptr;
}

class Window : public QWindow
{
public:
    Window(const QString &title, const QColor &bgColor, int axis, bool noVSync);
    ~Window();

protected:
    void init();
    void releaseResources();
    void resizeSwapChain();
    void releaseSwapChain();
    void render();

    void exposeEvent(QExposeEvent *) override;
    bool event(QEvent *) override;

    QColor m_bgColor;
    int m_rotationAxis;
    bool m_noVSync;

    bool m_running = false;
    bool m_notExposed = false;
    bool m_newlyExposed = false;

    QMatrix4x4 m_proj;
    QList<QRhiResource *> m_releasePool;

    bool m_hasSwapChain = false;
    QRhiSwapChain *m_sc = nullptr;
    QRhiRenderBuffer *m_ds = nullptr;
    QRhiRenderPassDescriptor *m_rp = nullptr;

    float m_rotation = 0;
    float m_opacity = 1;
    int m_opacityDir = -1;
};

Window::Window(const QString &title, const QColor &bgColor, int axis, bool noVSync)
    : m_bgColor(bgColor),
      m_rotationAxis(axis),
      m_noVSync(noVSync)
{
    switch (graphicsApi) {
    case OpenGL:
    {
        setSurfaceType(OpenGLSurface);
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setSwapInterval(noVSync ? 0 : 1);
        setFormat(fmt);
    }
        break;
    case Vulkan:
        setSurfaceType(VulkanSurface);
#if QT_CONFIG(vulkan)
        setVulkanInstance(r.instance);
#endif
        break;
    case D3D11:
    case D3D12:
        setSurfaceType(Direct3DSurface);
        break;
    case Metal:
        setSurfaceType(MetalSurface);
        break;
    default:
        break;
    }

    resize(800, 600);
    setTitle(title);
}

Window::~Window()
{
    releaseResources();
}

void Window::exposeEvent(QExposeEvent *)
{
    // initialize and start rendering when the window becomes usable for graphics purposes
    if (isExposed() && !m_running) {
        m_running = true;
        init();
        resizeSwapChain();
    }

    // stop pushing frames when not exposed (or size is 0)
    if ((!isExposed() || (m_hasSwapChain && m_sc->surfacePixelSize().isEmpty())) && m_running)
        m_notExposed = true;

    // continue when exposed again and the surface has a valid size.
    // note that the surface size can be (0, 0) even though size() reports a valid one...
    if (isExposed() && m_running && m_notExposed && !m_sc->surfacePixelSize().isEmpty()) {
        m_notExposed = false;
        m_newlyExposed = true;
    }

    // always render a frame on exposeEvent() (when exposed) in order to update
    // immediately on window resize.
    if (isExposed() && !m_sc->surfacePixelSize().isEmpty())
        render();
}

bool Window::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::UpdateRequest:
        render();
        break;

    case QEvent::PlatformSurface:
        // this is the proper time to tear down the swapchain (while the native window and surface are still around)
        if (static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            releaseSwapChain();
        break;

    default:
        break;
    }

    return QWindow::event(e);
}

void Window::init()
{
    m_sc = r.r->newSwapChain();
    m_ds = r.r->newRenderBuffer(QRhiRenderBuffer::DepthStencil,
                                QSize(),
                                1,
                                QRhiRenderBuffer::UsedWithSwapChainOnly);
    m_releasePool << m_ds;
    m_sc->setWindow(this);
    m_sc->setDepthStencil(m_ds);
    if (m_noVSync)
        m_sc->setFlags(QRhiSwapChain::NoVSync);

    m_rp = m_sc->newCompatibleRenderPassDescriptor();
    m_releasePool << m_rp;
    m_sc->setRenderPassDescriptor(m_rp);

    ensureSharedResources(m_rp);
}

void Window::releaseResources()
{
    qDeleteAll(m_releasePool);
    m_releasePool.clear();

    delete m_sc;
    m_sc = nullptr;
}

void Window::resizeSwapChain()
{
    m_hasSwapChain = m_sc->createOrResize();

    const QSize outputSize = m_sc->currentPixelSize();
    m_proj = r.r->clipSpaceCorrMatrix();
    m_proj.perspective(45.0f, outputSize.width() / (float) outputSize.height(), 0.01f, 1000.0f);
    m_proj.translate(0, 0, -4);
}

void Window::releaseSwapChain()
{
    if (m_hasSwapChain) {
        m_hasSwapChain = false;
        m_sc->destroy();
    }
}

void Window::render()
{
    if (!m_hasSwapChain || m_notExposed)
        return;

    // If the window got resized or got newly exposed, resize the swapchain.
    // (the newly-exposed case is not actually required by some
    // platforms/backends, but f.ex. Vulkan on Windows seems to need it)
    if (m_sc->currentPixelSize() != m_sc->surfacePixelSize() || m_newlyExposed) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
        m_newlyExposed = false;
    }

    QRhi::FrameOpResult result = r.r->beginFrame(m_sc);
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        resizeSwapChain();
        if (!m_hasSwapChain)
            return;
        result = r.r->beginFrame(m_sc);
    }
    if (result != QRhi::FrameOpSuccess) {
        requestUpdate();
        return;
    }

    QRhiCommandBuffer *cb = m_sc->currentFrameCommandBuffer();
    const QSize outputSizeInPixels = m_sc->currentPixelSize();

    QRhiResourceUpdateBatch *u = r.r->nextResourceUpdateBatch();
    if (d.initialUpdates) {
        u->merge(d.initialUpdates);
        d.initialUpdates->release();
        d.initialUpdates = nullptr;
    }

    m_rotation += 1.0f;
    QMatrix4x4 mvp = m_proj;
    mvp.rotate(m_rotation, m_rotationAxis == 0 ? 1 : 0, m_rotationAxis == 1 ? 1 : 0, m_rotationAxis == 2 ? 1 : 0);
    u->updateDynamicBuffer(d.ubuf, 0, 64, mvp.constData());
    m_opacity += m_opacityDir * 0.005f;
    if (m_opacity < 0.0f || m_opacity > 1.0f) {
        m_opacityDir *= -1;
        m_opacity = qBound(0.0f, m_opacity, 1.0f);
    }
    u->updateDynamicBuffer(d.ubuf, 64, 4, &m_opacity);

    cb->beginPass(m_sc->currentFrameRenderTarget(),
                  QColor::fromRgbF(float(m_bgColor.redF()), float(m_bgColor.greenF()), float(m_bgColor.blueF()), 1.0f),
                  { 1.0f, 0 },
                  u);

    cb->setGraphicsPipeline(d.ps);
    cb->setViewport({ 0, 0, float(outputSizeInPixels.width()), float(outputSizeInPixels.height()) });
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vbufBinding(d.vbuf, 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(3);

    cb->endPass();

    r.r->endFrame(m_sc);

    requestUpdate();
}

void createWindow(bool noVSync)
{
    static QColor colors[] = { Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::cyan, Qt::gray };
    const int n = d.windows.count();
    d.windows.append(new Window(QString::asprintf("Window #%d%s", n, noVSync ? " (no vsync)" : ""), colors[n % 6], n % 3, noVSync));
    d.windows.last()->show();
}

void closeWindow()
{
    delete d.windows.takeLast();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

#if defined(Q_OS_WIN)
    graphicsApi = D3D11;
#elif QT_CONFIG(metal)
    graphicsApi = Metal;
#elif QT_CONFIG(vulkan)
    graphicsApi = Vulkan;
#else
    graphicsApi = OpenGL;
#endif

    QCommandLineParser cmdLineParser;
    cmdLineParser.addHelpOption();
    QCommandLineOption glOption({ "g", "opengl" }, QLatin1String("OpenGL (2.x)"));
    cmdLineParser.addOption(glOption);
    QCommandLineOption vkOption({ "v", "vulkan" }, QLatin1String("Vulkan"));
    cmdLineParser.addOption(vkOption);
    QCommandLineOption d3dOption({ "d", "d3d11" }, QLatin1String("Direct3D 11"));
    cmdLineParser.addOption(d3dOption);
    QCommandLineOption d3d12Option({ "D", "d3d12" }, QLatin1String("Direct3D 12"));
    cmdLineParser.addOption(d3d12Option);
    QCommandLineOption mtlOption({ "m", "metal" }, QLatin1String("Metal"));
    cmdLineParser.addOption(mtlOption);
    cmdLineParser.process(app);
    if (cmdLineParser.isSet(glOption))
        graphicsApi = OpenGL;
    if (cmdLineParser.isSet(vkOption))
        graphicsApi = Vulkan;
    if (cmdLineParser.isSet(d3dOption))
        graphicsApi = D3D11;
    if (cmdLineParser.isSet(d3d12Option))
        graphicsApi = D3D12;
    if (cmdLineParser.isSet(mtlOption))
        graphicsApi = Metal;

    qDebug("Selected graphics API is %s", qPrintable(graphicsApiName()));
    qDebug("This is a multi-api example, use command line arguments to override:\n%s", qPrintable(cmdLineParser.helpText()));

    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

#if QT_CONFIG(vulkan)
    r.instance = new QVulkanInstance;
    if (graphicsApi == Vulkan) {
        r.instance->setLayers({ "VK_LAYER_KHRONOS_validation" });
        r.instance->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!r.instance->create()) {
            qWarning("Failed to create Vulkan instance, switching to OpenGL");
            graphicsApi = OpenGL;
        }
    }
#endif

    createRhi();

    int winCount = 0;
    QWidget w;
    w.resize(800, 600);
    w.setWindowTitle(QCoreApplication::applicationName() + QLatin1String(" - ") + graphicsApiName());
    QVBoxLayout *layout = new QVBoxLayout(&w);

    QPlainTextEdit *info = new QPlainTextEdit(
                QLatin1String("This application tests rendering with the same QRhi instance (and so the same Vulkan/Metal/D3D device or OpenGL context) "
                              "to multiple windows via multiple QRhiSwapChain objects, from the same one thread. Some resources are shared across all windows.\n"
                              "\nNote that the behavior may differ depending on the underlying graphics API implementation and the number of windows. "
                              "One challenge here is the vsync throttling: with the default vsync/fifo presentation mode the behavior may differ between "
                              "platforms, drivers, and APIs as we present different swapchains' images in a row on the same thread. As a potential solution, "
                              "setting NoVSync on the second, third, and later window swapchains is offered as an option.\n"
                              "\n\nUsing API: ") + graphicsApiName());
    info->setReadOnly(true);
    layout->addWidget(info);
    QLabel *label = new QLabel(QLatin1String("Window count: 0"));
    layout->addWidget(label);
    QCheckBox *vsCb = new QCheckBox(QLatin1String("Set NoVSync on all swapchains except the first"));
    vsCb->setChecked(false);
    layout->addWidget(vsCb);
    QPushButton *btn = new QPushButton(QLatin1String("New window"));
    QObject::connect(btn, &QPushButton::clicked, btn, [label, vsCb, &winCount] {
        winCount += 1;
        label->setText(QString::asprintf("Window count: %d", winCount));
        const bool noVSync = vsCb->isChecked() && winCount > 1;
        createWindow(noVSync);
    });
    layout->addWidget(btn);
    btn = new QPushButton(QLatin1String("Close window"));
    QObject::connect(btn, &QPushButton::clicked, btn, [label, &winCount] {
        if (winCount > 0) {
            winCount -= 1;
            label->setText(QString::asprintf("Window count: %d", winCount));
            closeWindow();
        }
    });
    layout->addWidget(btn);
    w.show();

    int result = app.exec();

    qDeleteAll(d.windows);

    destroySharedResources();
    destroyRhi();

#if QT_CONFIG(vulkan)
    delete r.instance;
#endif

    return result;
}
