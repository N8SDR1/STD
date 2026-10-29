// Copyright (C) 2026 Rick Langford (N8SDR)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QRhiWidget>

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

// Plot pixels for the waterfall, on the same Qt RHI backends Lyra uses
// (Vulkan, Direct3D 11, Direct3D 12, OpenGL). The software trace stays
// the fallback. This does not decode and does not key.
class WaterfallGpu : public QRhiWidget {
  Q_OBJECT
public:
  // Null means stay on the software trace.
  static Api resolve(const QString& choice, QString* liveName);
  // Reads the saved choice. A Painter value left by a failed init becomes
  // Auto. Auto is Vulkan. When applyEnvironment is true, software-renderer
  // variables are cleared and the widget compositor is aimed at this
  // backend before any window exists.
  static QString prepareProcessBackend(bool applyEnvironment);
  static QString processBackend();
  // Keeps one Vulkan instance for the process and installs it on the
  // top-level window. Required before a Vulkan QRhiWidget is shown.
  static bool attachVulkan(QWidget* topLevel);

  explicit WaterfallGpu(Api api, QWidget* parent = nullptr);

  void present(const QImage& image);

signals:
  void unavailable();
  // The name QRhi reports after the new widget has a device. Empty until then.
  void backendReady(const QString& name);

protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;

private:
  bool buildPipeline();
  void dropTexture();

  QRhi* device_ = nullptr;
  QRhiBuffer* vertices_ = nullptr;
  QRhiBuffer* clip_ = nullptr;
  QRhiTexture* texture_ = nullptr;
  QRhiSampler* sampler_ = nullptr;
  QRhiShaderResourceBindings* bindings_ = nullptr;
  QRhiGraphicsPipeline* pipeline_ = nullptr;
  QRhiRenderPassDescriptor* pass_ = nullptr;
  QImage pending_;
  QSize textureSize_;
  bool frameDirty_ = false;
  bool shadersReady_ = false;
  bool verticesSent_ = false;
  bool noted_ = false;
  bool backendNoted_ = false;
};
