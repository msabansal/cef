// Copyright 2019 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/osr/host_display_client_osr.h"

#include <map>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/shared_memory_mapping.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "cef/include/cef_process_message.h"
#include "cef/libcef/browser/osr/render_widget_host_view_osr.h"
#include "cef/libcef/browser/thread_util.h"
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

namespace {

// Matches viz::ReachVideoOutputDevice::kMaxSlots.
constexpr size_t kMaxReachVideoLeases = 8;

std::string TicksToString(base::TimeTicks ticks) {
  return base::NumberToString(ticks.since_origin().InMicroseconds());
}

// Reach.VideoSink.v2: [kind, popup, stream, generation, sequence, width,
// height, stride, timestamp_us, pixels, error, slot, damage x, y, width,
// height, outstanding, first_paint_us, read_start_us, read_end_us, sent_us,
// received_us]. The view sets the popup flag.
CefRefPtr<CefProcessMessage> CreateVideoSinkMessage(int kind,
                                                    const std::string& stream,
                                                    const std::string& error) {
  auto message = CefProcessMessage::Create("Reach.VideoSink.v2");
  auto values = message->GetArgumentList();
  values->SetInt(0, kind);
  values->SetInt(1, 0);
  values->SetString(2, stream);
  values->SetString(3, "0");
  values->SetString(4, "0");
  values->SetInt(5, 0);
  values->SetInt(6, 0);
  values->SetInt(7, 0);
  values->SetString(8, "0");
  values->SetNull(9);
  values->SetString(10, error);
  values->SetInt(11, 0);
  for (size_t i = 12; i <= 16; ++i) {
    values->SetInt(i, 0);
  }
  for (size_t i = 17; i <= 21; ++i) {
    values->SetString(i, "0");
  }
  return message;
}

}  // namespace

// Owns the consumer side of one compositor's snapshot ring. A delivered frame
// stays leased to the client until CefReleaseReachVideoFrame() names it, so the
// producer sees the client's real pipeline depth.
class CefReachVideoSink : public viz::mojom::ReachVideoSink {
 public:
  CefReachVideoSink(
      CefRenderWidgetHostViewOSR* view,
      mojo::PendingReceiver<viz::mojom::ReachVideoSink> receiver)
      : view_(view), receiver_(this, std::move(receiver)) {
    receiver_.set_disconnect_handler(base::BindOnce(
        &CefReachVideoSink::Disconnected, base::Unretained(this)));
  }

  ~CefReachVideoSink() override {
    Unregister();
    // Close the pipe before dropping unreleased replies.
    receiver_.reset();
    leases_.clear();
  }

  static base::WeakPtr<CefReachVideoSink> Find(const std::string& stream) {
    auto& sinks = Sinks();
    auto it = sinks.find(stream);
    if (it == sinks.end()) {
      return nullptr;
    }
    return it->second;
  }

  void Started(const std::string& stream) override {
    Unregister();
    if (stream.empty() || Sinks().contains(stream)) {
      Error("Invalid Reach video stream");
      return;
    }
    stream_ = stream;
    Sinks()[stream_] = weak_factory_.GetWeakPtr();
    view_->OnVideoSinkEvent(CreateVideoSinkMessage(0, stream_, ""), nullptr);
  }

  void FrameReady(uint32_t slot,
                  base::ReadOnlySharedMemoryRegion pixels,
                  const gfx::Size& size,
                  uint32_t stride,
                  uint64_t generation,
                  uint64_t sequence,
                  base::TimeTicks timestamp,
                  const gfx::Rect& damage,
                  uint32_t outstanding,
                  viz::mojom::ReachVideoFrameTimingPtr timing,
                  FrameReadyCallback released) override {
    const auto received = base::TimeTicks::Now();
    base::ScopedClosureRunner release(std::move(released));
    const uint64_t bytes = static_cast<uint64_t>(stride) * size.height();
    if (stream_.empty() || size.IsEmpty() || size.width() > 8192 ||
        size.height() > 8192 || stride != static_cast<uint32_t>(size.width()) * 4 ||
        bytes > 64 * 1024 * 1024 || !generation || sequence <= sequence_ ||
        slot >= kMaxReachVideoLeases || !outstanding ||
        outstanding > kMaxReachVideoLeases || damage.IsEmpty() ||
        !gfx::Rect(size).Contains(damage) || !timing ||
        leases_.size() >= kMaxReachVideoLeases) {
      Error("Invalid Reach video frame metadata");
      return;
    }
    sequence_ = sequence;
    auto mapping = pixels.Map();
    if (!mapping.IsValid() || mapping.size() != bytes) {
      Error("Invalid Reach video shared-memory frame");
      return;
    }
    auto event = CreateVideoSinkMessage(1, stream_, "");
    auto values = event->GetArgumentList();
    values->SetString(3, base::NumberToString(generation));
    values->SetString(4, base::NumberToString(sequence));
    values->SetInt(5, size.width());
    values->SetInt(6, size.height());
    values->SetInt(7, stride);
    values->SetString(8, TicksToString(timestamp));
    values->SetBinary(9, CefBinaryValue::Create(mapping.memory(), bytes));
    values->SetInt(11, slot);
    values->SetInt(12, damage.x());
    values->SetInt(13, damage.y());
    values->SetInt(14, damage.width());
    values->SetInt(15, damage.height());
    values->SetInt(16, outstanding);
    values->SetString(17, TicksToString(timing->first_paint));
    values->SetString(18, TicksToString(timing->read_start));
    values->SetString(19, TicksToString(timing->read_end));
    values->SetString(20, TicksToString(timing->sent));
    values->SetString(21, TicksToString(received));
    // The binary value owns a copy; the slot stays leased until the client
    // releases it.
    mapping = {};
    auto weak_this = weak_factory_.GetWeakPtr();
    const bool leased = view_->OnVideoSinkEvent(
        event, CreateVideoSinkMessage(0, stream_, ""));
    if (!leased || !weak_this) {
      return;
    }
    leases_.emplace(sequence, release.Release());
  }

  void Release(uint64_t sequence) {
    auto it = leases_.find(sequence);
    if (it == leases_.end()) {
      Error("Unknown or repeated Reach video frame release");
      return;
    }
    base::OnceClosure released = std::move(it->second);
    leases_.erase(it);
    std::move(released).Run();
  }

  void Error(const std::string& message) override {
    view_->OnVideoSinkEvent(CreateVideoSinkMessage(2, stream_, message),
                            nullptr);
  }

 private:
  using SinkMap = std::map<std::string, base::WeakPtr<CefReachVideoSink>>;

  static SinkMap& Sinks() {
    static base::NoDestructor<SinkMap> sinks;
    return *sinks;
  }

  void Unregister() {
    if (!stream_.empty()) {
      Sinks().erase(stream_);
    }
  }

  void Disconnected() { Error("Reach compositor video sink disconnected"); }

  const raw_ptr<CefRenderWidgetHostViewOSR> view_;
  mojo::Receiver<viz::mojom::ReachVideoSink> receiver_;
  std::string stream_;
  uint64_t sequence_ = 0;
  // Replies for frames leased to the client, keyed by sequence.
  std::map<uint64_t, base::OnceClosure> leases_;
  base::WeakPtrFactory<CefReachVideoSink> weak_factory_{this};
};

void CefReleaseReachVideoFrame(const std::string& stream, uint64_t sequence) {
  CEF_REQUIRE_UIT();
  // Releases for retired streams are harmless: their replies were dropped
  // with the closed pipe.
  if (auto sink = CefReachVideoSink::Find(stream)) {
    sink->Release(sequence);
  }
}

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
