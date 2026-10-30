// Copyright 2019 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/osr/host_display_client_osr.h"

#include <utility>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/shared_memory_mapping.h"
#include "cef/libcef/browser/osr/render_widget_host_view_osr.h"
#include "components/viz/common/resources/shared_image_format_utils.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "services/viz/privileged/mojom/compositing/layered_window_updater.mojom.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/src/core/SkDevice.h"
#include "ui/gfx/skia_util.h"

#if BUILDFLAG(IS_WIN)
#include "skia/ext/skia_utils_win.h"
#endif

class CefReachVideoSink : public viz::mojom::ReachVideoSink {
 public:
  CefReachVideoSink(
      CefRenderWidgetHostViewOSR* view,
      mojo::PendingReceiver<viz::mojom::ReachVideoSink> receiver)
      : view_(view), receiver_(this, std::move(receiver)) {
    receiver_.set_disconnect_handler(base::BindOnce(
        &CefReachVideoSink::Disconnected, base::Unretained(this)));
  }

  void Started(const std::string& stream) override {
    stream_ = stream;
    view_->OnVideoSinkEvent(0, stream_, "", 0, 0, gfx::Size(), 0, 0, nullptr, 0);
  }

  void FrameReady(base::ReadOnlySharedMemoryRegion pixels,
                  const gfx::Size& size,
                  uint32_t stride,
                  uint64_t generation,
                  uint64_t sequence,
                  base::TimeTicks timestamp,
                  FrameReadyCallback released) override {
    base::ScopedClosureRunner release(std::move(released));
    const uint64_t bytes = static_cast<uint64_t>(stride) * size.height();
    if (stream_.empty() || size.IsEmpty() || size.width() > 8192 ||
        size.height() > 8192 || stride != static_cast<uint32_t>(size.width()) * 4 ||
        bytes > 64 * 1024 * 1024 || !generation || !sequence) {
      Error("Invalid Reach video frame metadata");
      return;
    }
    auto mapping = pixels.Map();
    if (!mapping.IsValid() || mapping.size() != bytes) {
      Error("Invalid Reach video shared-memory frame");
      return;
    }
    view_->OnVideoSinkEvent(
        1, stream_, "", generation, sequence, size, stride,
        timestamp.since_origin().InMicroseconds(), mapping.memory(), bytes);
  }

  void Error(const std::string& message) override {
    view_->OnVideoSinkEvent(2, stream_, message, 0, 0, gfx::Size(), 0, 0, nullptr, 0);
  }

 private:
  void Disconnected() { Error("Reach compositor video sink disconnected"); }

  const raw_ptr<CefRenderWidgetHostViewOSR> view_;
  mojo::Receiver<viz::mojom::ReachVideoSink> receiver_;
  std::string stream_;
};

class CefLayeredWindowUpdaterOSR : public viz::mojom::LayeredWindowUpdater {
 public:
  CefLayeredWindowUpdaterOSR(
      CefRenderWidgetHostViewOSR* const view,
      mojo::PendingReceiver<viz::mojom::LayeredWindowUpdater> receiver);

  CefLayeredWindowUpdaterOSR(const CefLayeredWindowUpdaterOSR&) = delete;
  CefLayeredWindowUpdaterOSR& operator=(const CefLayeredWindowUpdaterOSR&) =
      delete;

  ~CefLayeredWindowUpdaterOSR() override;

  void SetActive(bool active);
  const void* GetPixelMemory() const;
  gfx::Size GetPixelSize() const;

  // viz::mojom::LayeredWindowUpdater implementation.
  void OnAllocatedSharedMemory(const gfx::Size& pixel_size,
                               base::UnsafeSharedMemoryRegion region) override;
  void Draw(const gfx::Rect& damage_rect, DrawCallback draw_callback) override;

 private:
  const raw_ptr<CefRenderWidgetHostViewOSR> view_;
  mojo::Receiver<viz::mojom::LayeredWindowUpdater> receiver_;
  bool active_ = false;
  base::WritableSharedMemoryMapping shared_memory_;
  gfx::Size pixel_size_;
};

CefLayeredWindowUpdaterOSR::CefLayeredWindowUpdaterOSR(
    CefRenderWidgetHostViewOSR* const view,
    mojo::PendingReceiver<viz::mojom::LayeredWindowUpdater> receiver)
    : view_(view), receiver_(this, std::move(receiver)) {}

CefLayeredWindowUpdaterOSR::~CefLayeredWindowUpdaterOSR() = default;

void CefLayeredWindowUpdaterOSR::SetActive(bool active) {
  active_ = active;
}

const void* CefLayeredWindowUpdaterOSR::GetPixelMemory() const {
  return shared_memory_.memory();
}

gfx::Size CefLayeredWindowUpdaterOSR::GetPixelSize() const {
  return pixel_size_;
}

void CefLayeredWindowUpdaterOSR::OnAllocatedSharedMemory(
    const gfx::Size& pixel_size,
    base::UnsafeSharedMemoryRegion region) {
  // Make sure |pixel_size| is sane.
  auto expected_bytes = SharedMemorySizeForSharedImageFormat(
      viz::SinglePlaneFormat::kRGBA_8888, pixel_size);
  if (!expected_bytes) {
    DLOG(ERROR) << "OnAllocatedSharedMemory with size that overflows";
    return;
  }

  auto mapping = region.Map();
  if (!mapping.IsValid()) {
    DLOG(ERROR) << "Shared memory mapping failed.";
    return;
  }
  if (mapping.size() < expected_bytes.value()) {
    DLOG(ERROR) << "Shared memory size was less than expected.";
    return;
  }

  pixel_size_ = pixel_size;
  shared_memory_ = std::move(mapping);
}

void CefLayeredWindowUpdaterOSR::Draw(const gfx::Rect& damage_rect,
                                      DrawCallback draw_callback) {
  if (active_) {
    const void* memory = GetPixelMemory();
    if (memory) {
      view_->OnPaint(damage_rect, pixel_size_, memory);
    } else {
      LOG(WARNING) << "Failed to read pixels";
    }
  }

  std::move(draw_callback).Run();
}

CefHostDisplayClientOSR::CefHostDisplayClientOSR(
    CefRenderWidgetHostViewOSR* const view,
    gfx::AcceleratedWidget widget)
    : viz::HostDisplayClient(widget), view_(view) {}

CefHostDisplayClientOSR::~CefHostDisplayClientOSR() = default;

void CefHostDisplayClientOSR::SetActive(bool active) {
  active_ = active;
  if (layered_window_updater_) {
    layered_window_updater_->SetActive(active_);
  }
}

const void* CefHostDisplayClientOSR::GetPixelMemory() const {
  return layered_window_updater_ ? layered_window_updater_->GetPixelMemory()
                                 : nullptr;
}

gfx::Size CefHostDisplayClientOSR::GetPixelSize() const {
  return layered_window_updater_ ? layered_window_updater_->GetPixelSize()
                                 : gfx::Size{};
}

void CefHostDisplayClientOSR::CreateLayeredWindowUpdater(
    mojo::PendingReceiver<viz::mojom::LayeredWindowUpdater> receiver) {
  CHECK(!base::CommandLine::ForCurrentProcess()->HasSwitch("reach-video-sink"));
  layered_window_updater_ =
      std::make_unique<CefLayeredWindowUpdaterOSR>(view_, std::move(receiver));
  layered_window_updater_->SetActive(active_);
}

void CefHostDisplayClientOSR::CreateReachVideoSink(
    mojo::PendingReceiver<viz::mojom::ReachVideoSink> receiver) {
  CHECK(base::CommandLine::ForCurrentProcess()->HasSwitch("reach-video-sink"));
  video_sink_ = std::make_unique<CefReachVideoSink>(view_, std::move(receiver));
}

#if BUILDFLAG(IS_LINUX) && BUILDFLAG(SUPPORTS_OZONE_X11)
void CefHostDisplayClientOSR::DidCompleteSwapWithNewSize(
    const gfx::Size& size) {}
#endif
