// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaterfallGpu.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <QFile>
#include <QMatrix4x4>
#include <QSettings>
#include <QTimer>
#include <QVulkanInstance>
#include <QWidget>
#include <QWindow>

namespace {

QString gProcessBackend = QStringLiteral("vulkan");

bool knownGraphicsKey(const QString& key) {
  const QString known[] = {QStringLiteral("auto"),   QStringLiteral("vulkan"), QStringLiteral("d3d11"),
                           QStringLiteral("d3d12"), QStringLiteral("opengl"), QStringLiteral("painter")};
  for (const QString& item : known) {
    if (key == item)
      return true;
  }
  return false;
}

void clearSoftwareRendererEnv() {
  qunsetenv("QSG_RHI_PREFER_SOFTWARE_RENDERER");
  const char* names[] = {"QSG_RHI_BACKEND", "QT_QUICK_BACKEND", "QT_OPENGL", "QT_WIDGETS_RHI_BACKEND"};
  for (const char* name : names) {
    const QByteArray value = qgetenv(name).trimmed().toLower();
    if (value == "software" || value == "sw" || value == "llvmpipe" || value == "painter" ||
        value == "null")
      qunsetenv(name);
  }
}

QShader loadShader(const char* path) {
  QFile file(QString::fromLatin1(path));
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QShader::fromSerialized(file.readAll());
}

const QShader& vertexShader() {
  static const QShader shader = loadShader(":/std/waterfall.vert.qsb");
  return shader;
}

const QShader& fragmentShader() {
  static const QShader shader = loadShader(":/std/waterfall.frag.qsb");
  return shader;
}

}  // namespace

QRhiWidget::Api WaterfallGpu::resolve(const QString& choice, QString* liveName) {
  const QString key = choice.trimmed().toLower();
  auto name = [&](const char* text) {
    if (liveName)
      *liveName = QString::fromLatin1(text);
  };
  if (key == QLatin1String("painter") || key == QLatin1String("software")) {
    name("Painter");
    return Api::Null;
  }
  if (key == QLatin1String("opengl")) {
    name("OpenGL");
    return Api::OpenGL;
  }
  if (key == QLatin1String("d3d12")) {
    name("Direct3D 12");
    return Api::Direct3D12;
  }
  if (key == QLatin1String("d3d11")) {
    name("Direct3D 11");
    return Api::Direct3D11;
  }
  // Empty, auto, and vulkan all request Vulkan. The instance is attached
  // separately. Painter is only the explicit software choice.
  name("Vulkan");
  return Api::Vulkan;
}

QString WaterfallGpu::processBackend() {
  return gProcessBackend;
}

QString WaterfallGpu::prepareProcessBackend(bool applyEnvironment) {
  clearSoftwareRendererEnv();
  QSettings settings(QStringLiteral("N8SDR"), QStringLiteral("STD"));
  QString key = settings.value(QStringLiteral("waterfall/graphics"), QStringLiteral("auto"))
                    .toString()
                    .trimmed()
                    .toLower();
  if (!knownGraphicsKey(key))
    key = QStringLiteral("auto");
  const int rev = settings.value(QStringLiteral("waterfall/graphicsRev"), 0).toInt();
  if (rev < 1 && (key == QLatin1String("painter") || key == QLatin1String("software"))) {
    key = QStringLiteral("auto");
    settings.setValue(QStringLiteral("waterfall/graphics"), key);
  }
  if (rev < 1)
    settings.setValue(QStringLiteral("waterfall/graphicsRev"), 1);

  QString backend = key;
  if (backend == QLatin1String("auto") || backend.isEmpty())
    backend = QStringLiteral("vulkan");
  gProcessBackend = backend;
  if (applyEnvironment && backend != QLatin1String("painter")) {
    qputenv("QT_WIDGETS_RHI", "1");
    qputenv("QT_WIDGETS_RHI_BACKEND", backend.toLatin1());
  }
  return backend;
}

bool WaterfallGpu::attachVulkan(QWidget* topLevel) {
  if (!topLevel)
    return false;
  QWidget* host = topLevel->window();
  if (!host)
    host = topLevel;
  host->winId();
  QWindow* handle = host->windowHandle();
  if (!handle)
    return false;

  static QVulkanInstance* instance = nullptr;
  static int ready = 0;
  if (ready == 0) {
    auto* created = new QVulkanInstance;
    created->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    if (created->create()) {
      instance = created;
      ready = 1;
    } else {
      delete created;
      ready = -1;
    }
  }
  if (ready != 1 || !instance)
    return false;
  if (handle->vulkanInstance() != instance)
    handle->setVulkanInstance(instance);
  return true;
}

WaterfallGpu::WaterfallGpu(Api api, QWidget* parent) : QRhiWidget(nullptr) {
  // setApi is ignored once the widget belongs to a window. The Windows
  // default is Direct3D 11, so a parent passed into QRhiWidget locks the
  // wrong API and the choice falls through to the software trace.
  if (api != Api::Null)
    setApi(api);
  setSampleCount(1);
  setColorBufferFormat(TextureFormat::RGBA8);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setFocusPolicy(Qt::NoFocus);
  connect(this, &QRhiWidget::renderFailed, this, [this] {
    if (noted_)
      return;
    noted_ = true;
    QTimer::singleShot(0, this, [this] { emit unavailable(); });
  });
  if (parent)
    setParent(parent);
}

void WaterfallGpu::present(const QImage& image) {
  if (image.isNull())
    return;
  pending_ = image.format() == QImage::Format_RGBA8888
                 ? image
                 : image.convertToFormat(QImage::Format_RGBA8888);
  frameDirty_ = true;
  update();
}

void WaterfallGpu::dropTexture() {
  if (!texture_)
    return;
  texture_->destroy();
  delete texture_;
  texture_ = nullptr;
  textureSize_ = {};
}

void WaterfallGpu::releaseResources() {
  auto drop = [](QRhiResource* resource) {
    if (!resource)
      return;
    resource->destroy();
    delete resource;
  };
  drop(pipeline_);
  pipeline_ = nullptr;
  drop(bindings_);
  bindings_ = nullptr;
  drop(sampler_);
  sampler_ = nullptr;
  dropTexture();
  drop(clip_);
  clip_ = nullptr;
  drop(vertices_);
  vertices_ = nullptr;
  pass_ = nullptr;
  device_ = nullptr;
  shadersReady_ = false;
  verticesSent_ = false;
  backendNoted_ = false;
}

bool WaterfallGpu::buildPipeline() {
  QRhi* device = rhi();
  QRhiRenderTarget* target = renderTarget();
  if (!device || !target || !target->renderPassDescriptor())
    return false;
  if (!vertexShader().isValid() || !fragmentShader().isValid())
    return false;

  if (!vertices_) {
    vertices_ = device->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 16 * sizeof(float));
    if (!vertices_->create())
      return false;
    verticesSent_ = false;
  }
  if (!clip_) {
    clip_ = device->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
    if (!clip_->create())
      return false;
  }
  if (!sampler_) {
    sampler_ = device->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge,
                                  QRhiSampler::ClampToEdge);
    if (!sampler_->create())
      return false;
  }
  if (!texture_) {
    texture_ = device->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    if (!texture_->create())
      return false;
    textureSize_ = QSize(1, 1);
    frameDirty_ = true;
  }
  if (bindings_) {
    bindings_->destroy();
    delete bindings_;
    bindings_ = nullptr;
  }
  bindings_ = device->newShaderResourceBindings();
  bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                              0, QRhiShaderResourceBinding::VertexStage, clip_),
                          QRhiShaderResourceBinding::sampledTexture(
                              1, QRhiShaderResourceBinding::FragmentStage, texture_, sampler_)});
  if (!bindings_->create())
    return false;

  if (pipeline_) {
    pipeline_->destroy();
    delete pipeline_;
    pipeline_ = nullptr;
  }
  QRhiVertexInputLayout layout;
  layout.setBindings({QRhiVertexInputBinding(4 * sizeof(float))});
  layout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float4, 0)});

  pipeline_ = device->newGraphicsPipeline();
  pipeline_->setTopology(QRhiGraphicsPipeline::TriangleStrip);
  pipeline_->setCullMode(QRhiGraphicsPipeline::None);
  pipeline_->setShaderStages({QRhiShaderStage(QRhiShaderStage::Vertex, vertexShader()),
                              QRhiShaderStage(QRhiShaderStage::Fragment, fragmentShader())});
  pipeline_->setVertexInputLayout(layout);
  pipeline_->setShaderResourceBindings(bindings_);
  pipeline_->setRenderPassDescriptor(target->renderPassDescriptor());
  pipeline_->setSampleCount(target->sampleCount());
  if (!pipeline_->create())
    return false;

  pass_ = target->renderPassDescriptor();
  device_ = device;
  shadersReady_ = true;
  return true;
}

void WaterfallGpu::initialize(QRhiCommandBuffer*) {
  if (device_ != rhi())
    releaseResources();
  QRhiRenderTarget* target = renderTarget();
  if (pipeline_ && target && pass_ != target->renderPassDescriptor()) {
    if (pipeline_) {
      pipeline_->destroy();
      delete pipeline_;
      pipeline_ = nullptr;
    }
    pass_ = nullptr;
  }
  if (!pipeline_ && !buildPipeline() && !noted_) {
    noted_ = true;
    releaseResources();
    QTimer::singleShot(0, this, [this] { emit unavailable(); });
    return;
  }
  if (pipeline_ && rhi() && !backendNoted_) {
    backendNoted_ = true;
    const QString name = QString::fromLatin1(rhi()->backendName());
    qInfo("waterfall rhi %s", rhi()->backendName());
    setAccessibleName(QStringLiteral("Waterfall %1").arg(name));
    QTimer::singleShot(0, this, [this, name] { emit backendReady(name); });
  }
}

void WaterfallGpu::render(QRhiCommandBuffer* cb) {
  QRhi* device = rhi();
  QRhiRenderTarget* target = renderTarget();
  if (!device || !target)
    return;

  QRhiResourceUpdateBatch* batch = device->nextResourceUpdateBatch();
  if (vertices_ && !verticesSent_) {
    const float quad[] = {
        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
    };
    batch->uploadStaticBuffer(vertices_, quad);
    verticesSent_ = true;
  }
  if (clip_) {
    const QMatrix4x4 clip = device->clipSpaceCorrMatrix();
    batch->updateDynamicBuffer(clip_, 0, 64, clip.constData());
  }
  if (frameDirty_ && !pending_.isNull() && sampler_ && shadersReady_) {
    if (pending_.size() != textureSize_) {
      dropTexture();
      texture_ = device->newTexture(QRhiTexture::RGBA8, pending_.size());
      if (!texture_->create() || !buildPipeline()) {
        batch->release();
        if (!noted_) {
          noted_ = true;
          QTimer::singleShot(0, this, [this] { emit unavailable(); });
        }
        cb->beginPass(target, QColor(QStringLiteral("#00020c")), {1.0f, 0});
        cb->endPass();
        return;
      }
      // buildPipeline() may have replaced the texture with a 1x1 if texture_
      // was null. The size check above already created the right texture and
      // buildPipeline keeps it when texture_ is set. Re-upload below.
    }
    if (texture_)
      batch->uploadTexture(texture_, pending_);
    frameDirty_ = false;
  }

  cb->beginPass(target, QColor(QStringLiteral("#00020c")), {1.0f, 0}, batch);
  if (pipeline_ && vertices_ && texture_) {
    cb->setGraphicsPipeline(pipeline_);
    const QSize pixels = target->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, float(pixels.width()), float(pixels.height())));
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput input(vertices_, 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4);
  }
  cb->endPass();
}
