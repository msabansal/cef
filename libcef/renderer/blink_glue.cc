// Copyright (c) 2012 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/renderer/blink_glue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/hash/hash.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_op.h"
#include "cc/paint/paint_op_buffer.h"
#include "cc/paint/paint_op_buffer_iterator.h"
#include "cc/paint/paint_record.h"
#include "cc/paint/paint_recorder.h"
#include "cc/paint/paint_shader.h"
#include "cc/trees/scroll_source_type.h"
#include "crypto/hash.h"
#include "skia/ext/font_utils.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/v8_cache_options.mojom-blink.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url_response.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame_client.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/public/web/web_view.h"
#include "third_party/blink/public/web/web_view_client.h"
#include "third_party/blink/renderer/bindings/core/v8/sanitize_script_errors.h"
#include "third_party/blink/renderer/bindings/core/v8/script_evaluation_result.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/core/animation/pending_animations.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/document_lifecycle.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/events/pointer_event_factory.h"
#include "third_party/blink/renderer/core/editing/serializers/serialization.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_state_observer.h"
#include "third_party/blink/renderer/core/frame/frame_owner.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/event_handler_registry.h"
#include "third_party/blink/renderer/core/paint/object_paint_properties.h"
#include "third_party/blink/renderer/platform/graphics/paint/geometry_mapper.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/canvas/html_canvas_element.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/media/html_media_element.h"
#include "third_party/blink/renderer/core/html/media/reach_media_journal.h"
#include "third_party/blink/renderer/core/input/context_menu_allowed_scope.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/layout/hit_test_location.h"
#include "third_party/blink/renderer/core/layout/hit_test_request.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/page/context_menu_controller.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/paint/paint_flags.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/video_painter.h"
#include "third_party/blink/renderer/core/script/classic_script.h"
#include "third_party/blink/renderer/core/scroll/scrollable_area.h"
#include "third_party/blink/renderer/modules/encryptedmedia/html_media_element_encrypted_media.h"
#include "third_party/blink/renderer/modules/webgl/reach_webgl_journal.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/v8_binding.h"
#include "third_party/blink/renderer/platform/graphics/canvas_2d_bitmap_provider.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_chunks_to_cc_layer.h"
#include "third_party/blink/renderer/platform/graphics/paint/cull_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_artifact.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_chunk.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_chunk_subset.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_record_builder.h"
#include "third_party/blink/renderer/platform/graphics/paint/property_tree_state.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_response.h"
#include "third_party/blink/renderer/platform/loader/fetch/script_fetch_options.h"
#include "third_party/blink/renderer/platform/scheduler/main_thread/main_thread_scheduler_impl.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkColorFilter.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkImageFilter.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "third_party/skia/include/core/SkSerialProcs.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkTextBlob.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/encode/SkPngRustEncoder.h"
#include "third_party/skia/include/utils/SkNoDrawCanvas.h"
#include "third_party/skia/src/core/SkFontDescriptor.h"
#include "third_party/skia/src/core/SkReachResource.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/gfx/geometry/skia_conversions.h"
#undef LOG

#include "base/logging.h"

namespace blink {
// Defined by the reach_frame_requests patch in chrome_client_impl.cc.
CORE_EXPORT void SetReachFrameRequestHook(void (*hook)(LocalFrame&));
}  // namespace blink

namespace blink_glue {

namespace {

// Local roots already reported as needing a visual update since their last
// drawing-command capture. Pointers are only compared, never dereferenced.
// Animation demand is separate: discovery must not consume it, and picture
// notification coalescing must not swallow a first RAF on an otherwise idle
// page. A document supplement retires with its document, not a reused pointer.
class PendingAnimation final : public blink::GarbageCollected<PendingAnimation>,
                               public blink::Supplement<blink::Document> {
 public:
  static const char kSupplementName[];

  static PendingAnimation& From(blink::Document& document) {
    auto* state =
        blink::Supplement<blink::Document>::From<PendingAnimation>(document);
    if (!state) {
      state = blink::MakeGarbageCollected<PendingAnimation>(document);
      blink::Supplement<blink::Document>::ProvideTo(document, state);
    }
    return *state;
  }

  explicit PendingAnimation(blink::Document& document)
      : blink::Supplement<blink::Document>(document) {}

  void Trace(blink::Visitor* visitor) const override {
    blink::Supplement<blink::Document>::Trace(visitor);
  }

  bool pending = false;
};

const char PendingAnimation::kSupplementName[] = "ReachPendingAnimation";

std::function<void(blink::WebLocalFrame*)>& WebGlChangeCallback();

std::set<const blink::LocalFrame*>& RequestedFrameRoots() {
  static base::NoDestructor<std::set<const blink::LocalFrame*>> roots;
  return *roots;
}

std::function<void(blink::WebLocalFrame*)>& FrameRequestCallback() {
  static base::NoDestructor<std::function<void(blink::WebLocalFrame*)>> callback;
  return *callback;
}

void OnFrameRequested(blink::LocalFrame& root) {
  if (blink::Canvas2DBitmapProvider::IsReachWebGLCommandMode() &&
      root.GetDocument()) {
    auto& demand = PendingAnimation::From(*root.GetDocument());
    if (!demand.pending) {
      demand.pending = true;
      if (WebGlChangeCallback()) {
        if (auto* web_frame = blink::WebLocalFrameImpl::FromFrame(root))
          WebGlChangeCallback()(web_frame);
      }
    }
  }
  if (!FrameRequestCallback() || !RequestedFrameRoots().insert(&root).second)
    return;
  if (auto* web_frame = blink::WebLocalFrameImpl::FromFrame(root))
    FrameRequestCallback()(web_frame);
}

// Media playback state is part of the captured picture, but changes such as
// pause, seek or volume need not schedule a repaint. Captured windows request
// a frame on every media state event, so no periodic capture is needed.
class MediaStateFrameRequester final : public blink::NativeEventListener {
 public:
  void Invoke(blink::ExecutionContext* context, blink::Event*) override {
    auto* window = blink::DynamicTo<blink::LocalDOMWindow>(context);
    if (window && window->GetFrame())
      OnFrameRequested(window->GetFrame()->LocalFrameRoot());
  }
};

// Blink ignores a repeated (listener, capture) registration, so this is
// idempotent per window.
void ObserveMediaState(blink::LocalDOMWindow* window) {
  static base::NoDestructor<blink::Persistent<MediaStateFrameRequester>>
      listener(blink::MakeGarbageCollected<MediaStateFrameRequester>());
  if (!window) return;
  for (const auto* type :
       {&blink::event_type_names::kPlay, &blink::event_type_names::kPause,
        &blink::event_type_names::kPlaying, &blink::event_type_names::kSeeked,
        &blink::event_type_names::kRatechange,
        &blink::event_type_names::kVolumechange,
        &blink::event_type_names::kEnded, &blink::event_type_names::kEmptied,
        &blink::event_type_names::kLoadstart,
        &blink::event_type_names::kLoadedmetadata}) {
    window->addEventListener(*type, listener->Get(), /*use_capture=*/true);
  }
}

}  // namespace

void SetDrawingFrameRequestCallback(
    std::function<void(blink::WebLocalFrame*)> callback) {
  if (!blink::Canvas2DBitmapProvider::IsReachCommandMode()) return;
  FrameRequestCallback() = std::move(callback);
  blink::SetReachFrameRequestHook(&OnFrameRequested);
  // MediaSource operations are journaled for client replay, and each one
  // requests a frame so the client learns the journal's new position.
  blink::ReachMediaJournal::Enable(&OnFrameRequested);
}

namespace {

std::function<void(blink::WebLocalFrame*)>& WebGlChangeCallback() {
  static base::NoDestructor<std::function<void(blink::WebLocalFrame*)>> callback;
  return *callback;
}

void OnWebGlChanged(blink::HTMLCanvasElement* element) {
  auto* frame = element ? element->GetDocument().GetFrame() : nullptr;
  if (!frame || !frame->IsAttached())
    return;
  auto& root = frame->LocalFrameRoot();
  if (blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode()) {
    OnFrameRequested(root);
    return;
  }
  if (WebGlChangeCallback()) {
    if (auto* web_frame = blink::WebLocalFrameImpl::FromFrame(root))
      WebGlChangeCallback()(web_frame);
  }
}

class WebGlRetirement final : public blink::GarbageCollected<WebGlRetirement>,
                              public blink::Supplement<blink::Document> {
 public:
  static const char kSupplementName[];

  explicit WebGlRetirement(blink::Document& document)
      : blink::Supplement<blink::Document>(document),
        wake_(std::make_shared<Wake>(document)) {}

  static std::function<void()> Notification(blink::Document& document) {
    auto* retirement =
        blink::Supplement<blink::Document>::From<WebGlRetirement>(document);
    if (!retirement) {
      retirement = blink::MakeGarbageCollected<WebGlRetirement>(document);
      blink::Supplement<blink::Document>::ProvideTo(document, retirement);
    }
    return [wake = retirement->wake_] {
      // Finalization must not inspect canvas, document, frame, or weak
      // registry. One non-GC task state per document coalesces all collected
      // journals.
      if (wake->pending.exchange(true))
        return;
      wake->runner->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](std::shared_ptr<Wake> state) {
                state->pending.store(false);
                auto* current_document = state->document.Get();
                auto* frame =
                    current_document ? current_document->GetFrame() : nullptr;
                if (!frame || !frame->IsAttached() ||
                    frame->GetDocument() != current_document)
                  return;
                if (WebGlChangeCallback()) {
                  if (auto* web_frame = blink::WebLocalFrameImpl::FromFrame(
                          frame->LocalFrameRoot()))
                    WebGlChangeCallback()(web_frame);
                }
              },
              wake));
    };
  }

  void Trace(blink::Visitor* visitor) const override {
    blink::Supplement<blink::Document>::Trace(visitor);
  }

 private:
  struct Wake {
    explicit Wake(blink::Document& owner)
        : runner(base::SingleThreadTaskRunner::GetCurrentDefault()),
          document(&owner) {}

    scoped_refptr<base::SingleThreadTaskRunner> runner;
    blink::WeakPersistent<blink::Document> document;
    std::atomic_bool pending{false};
  };

  std::shared_ptr<Wake> wake_;
};

const char WebGlRetirement::kSupplementName[] = "ReachWebGlRetirement";

std::function<void()> OnWebGlRetirement(blink::HTMLCanvasElement* element) {
  return WebGlRetirement::Notification(element->GetDocument());
}

bool WebGlDecimal(const std::string& text, uint64_t* value) {
  return !text.empty() && text.size() <= 20 &&
         base::StringToUint64(text, value) &&
         base::NumberToString(*value) == text;
}

}  // namespace

void SetWebGlChangeCallback(
    std::function<void(blink::WebLocalFrame*)> callback) {
  if (!blink::Canvas2DBitmapProvider::IsReachWebGLCommandMode() &&
      !blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode())
    return;
  WebGlChangeCallback() = std::move(callback);
  if (blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode()) {
    blink::CanvasRenderingContext::SetReachCanvasVideoChangeCallback(
        &OnWebGlChanged);
    return;
  }
  blink::ReachWebGLJournal::SetChangeCallback(&OnWebGlChanged);
  blink::ReachWebGLJournal::SetRetirementCallback(&OnWebGlRetirement);
  blink::ReachWebGLJournal::SetOriginalHasher(
      [](base::span<const uint8_t> bytes) {
        return sk_reach::Hex(crypto::hash::Sha256(std::string_view(
            reinterpret_cast<const char*>(bytes.data()), bytes.size())));
      });
}

std::string WebGlRequest(
    blink::WebLocalFrame* web_frame,
    const std::string& operation,
    const std::string& document,
    const std::string& stream,
    const std::string& generation,
    const std::string& position,
    const std::string& offset,
    int max_bytes,
    std::vector<uint8_t>* output,
    std::map<std::string, std::vector<uint8_t>>* originals) {
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetDocument() ||
      !blink::Canvas2DBitmapProvider::IsReachWebGLCommandMode()) {
    return "WebGL request frame is detached or disabled";
  }
  if (operation != "Reach.WebGlStreams.v2" &&
      operation != "Reach.WebGlTick.v2" &&
      frame->GetDocument()->Token().ToString() != document) {
    return "WebGL native document identity is stale";
  }
  auto owned = [frame](blink::ReachWebGLJournal* journal) {
    auto* element = journal ? journal->element() : nullptr;
    auto* owner = element ? element->GetDocument().GetFrame() : nullptr;
    return owner && owner->IsAttached() &&
           owner->GetDocument() == &element->GetDocument() &&
           &owner->LocalFrameRoot() == frame;
  };
  // Command mode holds compositor begin frames. Service animation script
  // explicitly without capturing a picture or enabling compositor raster.
  blink::Persistent<blink::LocalFrame> retained_frame = frame;
  if (operation == "Reach.WebGlTick.v2") {
    blink::Persistent<blink::Document> retained_document = frame->GetDocument();
    blink::Persistent<blink::Page> page = frame->GetPage();
    if (!page || !frame->View())
      return "WebGL animation frame is unavailable";
    // Clear before entering script/lifecycle. A request made by that work
    // rearms demand and emits a new wakeup, even without a WebGL journal.
    PendingAnimation::From(*retained_document).pending = false;
    static_cast<blink::scheduler::MainThreadSchedulerImpl&>(
        blink::scheduler::WebThreadScheduler::MainThreadScheduler())
        .BeginFrameNotExpectedSoon();
    page->Animator().ServiceScriptedAnimations(base::TimeTicks::Now());
    if (!frame->IsAttached() || frame->GetDocument() != retained_document.Get() ||
        frame->GetPage() != page.Get() || !frame->View())
      return "WebGL animation document changed";
    page->Animator().UpdateAllLifecyclePhasesExceptPaint(
        *frame, blink::DocumentUpdateReason::kInspector);
    if (!frame->IsAttached() || frame->GetDocument() != retained_document.Get() ||
        frame->GetPage() != page.Get() || !frame->View())
      return "WebGL animation document changed";
  }
  if (operation == "Reach.WebGlStreams.v2" ||
      operation == "Reach.WebGlTick.v2") {
    const char* pending = PendingAnimation::From(*frame->GetDocument()).pending
                              ? "true"
                              : "false";
    std::string json = "{\"document\":\"" +
                       frame->GetDocument()->Token().ToString() +
                       "\",\"animationPending\":" + pending + ",\"streams\":[";
    bool first = true;
    for (auto* journal : blink::ReachWebGLJournal::Current()) {
      if (!owned(journal))
        continue;
      journal->ClearChangeNotification();
      const auto state = journal->State();
      if (state.empty())
        return "WebGL stream is unavailable: " + journal->error();
      if (!first)
        json += ',';
      first = false;
      json += "{\"state\":" + state + ",\"retired\":\"" +
              base::NumberToString(journal->retired()) + "\"}";
    }
    json += "]}";
    output->assign(json.begin(), json.end());
    return {};
  }
  uint64_t id = 0, context_generation = 0, cursor = 0;
  if (!WebGlDecimal(stream, &id) ||
      !WebGlDecimal(generation, &context_generation) ||
      !WebGlDecimal(position, &cursor)) {
    return "Invalid WebGL stream identity or sequence";
  }
  auto* journal = blink::ReachWebGLJournal::Find(id);
  if (!owned(journal) || journal->generation() != context_generation)
    return "WebGL stream unavailable or generation stale";
  if (operation == "Reach.WebGlApplied.v2" ||
      operation == "Reach.WebGlSent.v2") {
    const auto error = operation == "Reach.WebGlApplied.v2"
                           ? journal->Applied(context_generation, cursor)
                           : journal->Sent(context_generation, cursor);
    if (!error.empty())
      return error;
    const std::string json = "{\"stream\":\"" + stream +
                             "\",\"generation\":\"" + generation +
                             "\",\"through\":\"" + position + "\"}";
    output->assign(json.begin(), json.end());
    return {};
  }
  if (operation == "Reach.WebGlCommandSlice.v2" ||
      operation == "Reach.WebGlOriginalSlice.v2") {
    uint64_t start = 0;
    if (!WebGlDecimal(offset, &start) || start > 256 * 1024 * 1024 ||
        max_bytes <= 0 ||
        max_bytes > static_cast<int>(blink::ReachWebGLPending::kMaxSlice))
      return "Invalid WebGL byte slice offset or budget";
    return journal->ReadSlice(
        cursor, static_cast<size_t>(start), static_cast<size_t>(max_bytes),
        operation == "Reach.WebGlOriginalSlice.v2", output, originals);
  }
  if (operation != "Reach.WebGlRead.v2" || max_bytes <= 256 ||
      max_bytes > 8 * 1024 * 1024)
    return "Invalid WebGL read budget";
  return journal->Read(cursor, static_cast<size_t>(max_bytes), output, originals);
}

std::string ReadDrawingMediaJournal(blink::WebLocalFrame* web_frame,
                                    const std::string& journal_id,
                                    const std::string& cursor_text,
                                    int max_bytes,
                                    std::vector<uint8_t>* out) {
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached()) {
    return "Media journal frame is detached";
  }
  uint64_t id = 0;
  uint64_t cursor = 0;
  if (!base::StringToUint64(journal_id, &id) ||
      !base::StringToUint64(cursor_text, &cursor) || max_bytes <= 0) {
    return "Invalid media journal request";
  }
  if (id & (uint64_t{1} << 63)) {
    return "WebGL streams require the distinct v2 live API";
  }
  auto* journal = blink::ReachMediaJournal::Find(id);
  auto* element = journal ? journal->element() : nullptr;
  auto* element_frame = element ? element->GetDocument().GetFrame() : nullptr;
  // Journals of other tabs sharing this renderer are not readable.
  if (!element_frame || &element_frame->LocalFrameRoot() != frame) {
    return "Media journal unavailable";
  }
  *out = journal->Read(cursor, static_cast<size_t>(max_bytes));
  return {};
}

namespace {

constexpr size_t kMaxPictureBytes = 8 * 1024 * 1024;
constexpr size_t kMaxPaintBytes = 32 * 1024 * 1024;
constexpr size_t kMaxFilterDepth = 32;
// Shared filter nodes are revisited per reference; bound total traversal work
// so a small graph cannot fan out exponentially. Not a capability cap.
constexpr size_t kMaxFilterVisits = 1 << 16;
constexpr size_t kMaxRecordDepth = 32;
constexpr uint64_t kMaxViewportPixels = 4 * 1024 * 1024;
constexpr uint64_t kMaxDecodedImagePixels = 16 * 1024 * 1024;

class BoundedPaintStream final : public SkWStream {
 public:
  explicit BoundedPaintStream(size_t limit) : limit_(limit) {}

  bool write(const void* data, size_t size) override {
    if (failed_ || size > limit_ - bytes_.size()) {
      failed_ = true;
      return false;
    }
    if (size != 0) {
      const auto* start = static_cast<const uint8_t*>(data);
      bytes_.insert(bytes_.end(), start, start + size);
    }
    return true;
  }

  size_t bytesWritten() const override { return bytes_.size(); }
  bool failed() const { return failed_; }
  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

 private:
  const size_t limit_;
  bool failed_ = false;
  std::vector<uint8_t> bytes_;
};

using ImageStates = std::map<sk_reach::Hash, std::pair<size_t, bool>>;

struct PaintSerialization {
  std::string error;
  std::string unsupported_error;
  std::string filter_requires_raster;
  bool uninspectable_filter = false;
  bool raster = false;
  bool budget_only = false;
  bool uninspectable_originals = false;
  std::set<uint32_t> budget_images;
  std::set<uint32_t> budget_typefaces;
  ImageStates image_states;
  ImageStates all_image_states;
  size_t image_metadata_bytes = 0;
  std::function<void(const std::string&, const void*, size_t)> sink;
  // Originals the consumer already holds (streaming root captures only): they
  // are announced by identity, without bytes.
  const std::set<std::string>* known_originals = nullptr;
  std::set<std::string> original_ids;
  size_t identity_bytes = 0;
  size_t resources = 0;
  size_t bytes = 0;
  uint64_t image_pixels = 0;
  size_t filter_visits = 0;
  size_t record_bytes = 0;
  std::set<uint32_t> media_ids;
  std::set<uint32_t> culled_paint_ids;
  bool collect_paint_ids = false;
  std::map<std::string, std::vector<uint8_t>> originals;
  size_t font_bytes = 0;
  size_t fonts = 0;
  size_t typefaces = 0;
  // Incremented whenever a child-frame or media marker is played back.
  size_t markers = 0;
  // When set, every original export, in order, including repeats.
  struct Exported {
    uint8_t kind;
    sk_reach::Hash hash;
    size_t size;
    uint64_t pixels;
  };
  std::vector<Exported>* exports = nullptr;

  bool Export(uint8_t kind,
              const sk_reach::Hash& hash,
              size_t size,
              const std::function<sk_sp<const SkData>()>& load,
              uint64_t pixels = 0) {
    if (!size)
      return false;
    if (exports)
      exports->push_back({kind, hash, size, pixels});
    const std::string key =
        (kind == sk_reach::kImage ? "image:" : "font:") + sk_reach::Hex(hash);
    if (original_ids.contains(key))
      return true;
    if (kind == sk_reach::kImage) {
      if (pixels > kMaxDecodedImagePixels - image_pixels) {
        error = "Decoded image pixel budget exceeded";
        return false;
      }
      if (!AddResource(size))
        return false;
      image_pixels += pixels;
    } else {
      if (size > sk_reach::kFontBytes - font_bytes) {
        error = "Original font resource byte budget exceeded";
        return false;
      }
      ++fonts;
      font_bytes += size;
    }
    identity_bytes += key.size() + 64;
    if (bytes + font_bytes + identity_bytes > kMaxPaintBytes) {
      error = "Aggregate original resource budget exceeded";
      return false;
    }
    original_ids.insert(key);
    if (budget_only) return true;
    if (sink && known_originals && known_originals->contains(key)) {
      sink(key, nullptr, 0);
      return true;
    }
    const auto data = load();
    if (!data || data->size() != size) {
      error = "Original resource changed during serialization";
      return false;
    }
    if (sink) {
      sink(key, data->data(), data->size());
    } else {
      const auto* start = static_cast<const uint8_t*>(data->data());
      originals.emplace(key, std::vector<uint8_t>(start, start + data->size()));
    }
    return true;
  }

  bool AddResource(size_t size) {
    if (!error.empty()) {
      return false;
    }
    // Originals are bounded by the aggregate paint budget, not per resource.
    if (size == 0 || size > kMaxPaintBytes - bytes) {
      error = "Aggregate original resource budget exceeded or resource "
              "unavailable";
      return false;
    }
    ++resources;
    bytes += size;
    return true;
  }
};

// Original digests by Skia identity. Image and typeface unique IDs are never
// reused within the renderer and their bytes are immutable, so repeated
// captures neither re-read nor re-hash unchanged originals. Bounded, FIFO.
struct OriginalDigest {
  sk_reach::Hash hash;
  size_t size = 0;
  int collection_index = 0;
};

class OriginalDigests {
 public:
  const OriginalDigest* Find(uint32_t id) const {
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
  }
  void Insert(uint32_t id, const OriginalDigest& digest) {
    if (!entries_.insert_or_assign(id, digest).second) return;
    order_.push_back(id);
    if (order_.size() > 2048) {
      entries_.erase(order_.front());
      order_.pop_front();
    }
  }

 private:
  std::map<uint32_t, OriginalDigest> entries_;
  std::deque<uint32_t> order_;
};

OriginalDigests& ImageDigests() {
  static base::NoDestructor<OriginalDigests> digests;
  return *digests;
}

OriginalDigests& TypefaceDigests() {
  static base::NoDestructor<OriginalDigests> digests;
  return *digests;
}

sk_reach::Hash Digest(const SkData& data) {
  return crypto::hash::Sha256(std::string_view(
      static_cast<const char*>(data.data()), data.size()));
}

OriginalDigest ImageDigest(SkImage* image, const SkData& encoded) {
  const auto* known = ImageDigests().Find(image->uniqueID());
  if (known && known->size == encoded.size()) return *known;
  OriginalDigest digest{Digest(encoded), encoded.size()};
  ImageDigests().Insert(image->uniqueID(), digest);
  return digest;
}

SkSerialReturnType SerializeOriginalImage(SkImage* image, void* context) {
  auto* state = static_cast<PaintSerialization*>(context);
  if (state->error.empty() &&
      (image->isTextureBacked() || image->width() <= 0 ||
       image->height() <= 0 || image->width() > 4096 ||
       image->height() > 4096)) {
    state->error = "Texture-backed or oversized image requires command support";
  }
  const uint64_t pixels =
      static_cast<uint64_t>(image->width()) * image->height();
  if (state->error.empty()) {
    auto encoded = image->refEncodedData();
    if (encoded && encoded->size()) {
      if (encoded->size() > kMaxPaintBytes) {
        state->error = "Aggregate original resource budget exceeded";
        return SkData::MakeEmpty();
      }
      const auto digest = ImageDigest(image, *encoded);
      if (state->Export(
              sk_reach::kImage, digest.hash, digest.size,
              [&] { return sk_sp<const SkData>(encoded); }, pixels)) {
        if (state->budget_only)
          return SkData::MakeEmpty();
        return sk_reach::Write(sk_reach::kImage, digest.hash, digest.size);
      }
    }
    if (state->error.empty()) {
      static base::NoDestructor<std::set<uint32_t>> diagnostics;
      const char* timing = getenv("REACH_DRAWING_TIMING");
      if (diagnostics->size() < 16 && timing &&
          std::string_view(timing) == "1" &&
          diagnostics->insert(image->uniqueID()).second) {
        fprintf(stderr,
                "ReachUnencodedImage width=%d height=%d image=%u lazy=%d "
                "color=%d alpha=%d budget_only=%d\n",
                image->width(), image->height(), image->uniqueID(),
                image->isLazyGenerated(), static_cast<int>(image->colorType()),
                static_cast<int>(image->alphaType()), state->budget_only);
      }
      state->error =
          "Image has no original encoded resource; raster fallback rejected";
    }
  }
  // Never defer to Skia's default serializer on failure. The caller rejects
  // the entire picture after serialization.
  return SkData::MakeEmpty();
}

SkSerialReturnType SerializeOriginalTypeface(SkTypeface* face, void* context) {
  auto* state = static_cast<PaintSerialization*>(context);
  if (state->error.empty()) {
    sk_sp<SkData> data;
    OriginalDigest digest;
    if (const auto* known = TypefaceDigests().Find(face->uniqueID())) {
      digest = *known;
    } else {
      int collection_index = 0;
      auto original = face->openStream(&collection_index);
      if (!original || original->getLength() == 0) {
        state->error = "Original font stream unavailable";
      } else if (original->getLength() > sk_reach::kFontBytes) {
        state->error = "Original font resource byte budget exceeded";
      } else {
        data = SkData::MakeFromStream(original.get(), original->getLength());
        if (data && data->size()) {
          digest = {Digest(*data), data->size(), collection_index};
          TypefaceDigests().Insert(face->uniqueID(), digest);
        }
      }
    }
    const int collection_index = digest.collection_index;
    const auto load = [&]() -> sk_sp<const SkData> {
      if (!data) {
        int index = 0;
        auto original = face->openStream(&index);
        if (original && index == collection_index &&
            original->getLength() == digest.size) {
          data = SkData::MakeFromStream(original.get(), original->getLength());
        }
      }
      return data;
    };
    if (state->error.empty() && digest.size &&
        state->Export(sk_reach::kFont, digest.hash, digest.size, load)) {
      const auto& hash = digest.hash;
      {
        SkFontDescriptor descriptor;
        bool local = false;
        face->getFontDescriptor(&descriptor, &local);
        descriptor.detachStream();
        descriptor.setCollectionIndex(collection_index);
        descriptor.setSyntheticBold(face->isSyntheticBold());
        descriptor.setSyntheticOblique(face->isSyntheticOblique());
        const int axes = face->getVariationDesignPosition({});
        if (axes > 0) {
          if (axes > 256 || face->getVariationDesignPosition(
                  {descriptor.setVariationCoordinates(axes), size_t(axes)}) != axes) {
            state->error = "Original font variation metadata unavailable";
            return SkData::MakeEmpty();
          }
        }
        BoundedPaintStream stream(sk_reach::kDescriptorBytes);
        if (descriptor.serialize(&stream) && !stream.failed()) {
          if (state->budget_only) return SkData::MakeEmpty();
          const auto bytes = stream.TakeBytes();
          auto meta = SkData::MakeWithCopy(bytes.data(), bytes.size());
          return sk_reach::Write(sk_reach::kFont, hash, digest.size, meta.get());
        }
      }
    }
    if (state->error.empty()) {
      state->error = "Original font serialization exceeded the resource budget";
    }
  }
  return SkData::MakeEmpty();
}

struct OriginalTypefaceCache {
  std::map<std::array<uint8_t, 32>, sk_sp<SkTypeface>> faces;
  std::map<std::pair<std::array<uint8_t, 32>, std::optional<SkAlphaType>>,
           sk_sp<SkImage>> images;
  bool failed = false;
  const std::map<std::string, std::vector<uint8_t>>* originals = nullptr;

  sk_sp<SkData> Resolve(const sk_reach::Reference& ref) {
    if (!originals) return nullptr;
    const auto found = originals->find(
        (ref.kind == sk_reach::kImage ? "image:" : "font:") + sk_reach::Hex(ref.hash));
    if (found == originals->end() || found->second.size() != ref.length ||
        crypto::hash::Sha256(std::string_view(
            reinterpret_cast<const char*>(found->second.data()), found->second.size())) != ref.hash)
      return nullptr;
    return SkData::MakeWithCopy(found->second.data(), found->second.size());
  }
};

sk_sp<SkImage> DeserializeOriginalImage(sk_sp<SkData> data,
                                      std::optional<SkAlphaType> alpha,
                                      void* context) {
  auto* cache = static_cast<OriginalTypefaceCache*>(context);
  sk_reach::Reference ref;
  SkMemoryStream stream(data);
  if (!data || data->size() != sk_reach::kHeaderBytes ||
      !sk_reach::Read(stream, sk_reach::kImage, &ref) ||
      !(data = cache->Resolve(ref))) {
    cache->failed = true;
    return nullptr;
  }
  const auto key = std::make_pair(
      crypto::hash::Sha256(std::string_view(
          static_cast<const char*>(data->data()), data->size())),
      alpha);
  if (const auto found = cache->images.find(key); found != cache->images.end())
    return found->second;
  auto image = SkImages::DeferredFromEncodedData(std::move(data), alpha);
  if (!image) {
    cache->failed = true;
    return nullptr;
  }
  cache->images.emplace(key, image);
  return image;
}

sk_sp<SkTypeface> DeserializeOriginalTypeface(SkStream& stream, void* context) {
  auto* cache = static_cast<OriginalTypefaceCache*>(context);
  sk_reach::Reference ref;
  SkFontDescriptor descriptor;
  if (!sk_reach::Read(stream, sk_reach::kFont, &ref) ||
      !sk_reach::Descriptor(ref, &descriptor)) {
    cache->failed = true;
    return nullptr;
  }
  SkDynamicMemoryWStream identity;
  identity.write(ref.hash.data(), ref.hash.size());
  identity.write(ref.descriptor->data(), ref.descriptor->size());
  auto identity_data = identity.detachAsData();
  const auto key = crypto::hash::Sha256(std::string_view(
      static_cast<const char*>(identity_data->data()), identity_data->size()));
  if (const auto found = cache->faces.find(key); found != cache->faces.end())
    return found->second;
  auto data = cache->Resolve(ref);
  auto face = data ? skia::DefaultFontMgr()->makeFromStream(
      SkMemoryStream::Make(std::move(data)), descriptor.getFontArguments()) : nullptr;
  if (!face) {
    cache->failed = true;
    return nullptr;
  }
  cache->faces.emplace(key, face);
  return face;
}

void DeferUnsupportedPaint(PaintSerialization* state, std::string error) {
  // Unsupported categories never render, but must not hide a later budget
  // failure. Choose a stable rejection regardless of sibling/operation order.
  if (state->unsupported_error.empty() || error < state->unsupported_error) {
    state->unsupported_error = std::move(error);
  }
}

bool CheckPaintImage(PaintSerialization* state, const cc::PaintImage& image) {
  if (state->raster) {
    const uint64_t pixels = uint64_t(std::max(0, image.width())) *
                            std::max(0, image.height());
    if (image.IsTextureBacked() || image.IsDeferredPaintRecord()) {
      DeferUnsupportedPaint(
          state, "Raster fallback cannot read texture-backed or deferred paint");
    }
    if (!pixels || image.width() > 4096 || image.height() > 4096 ||
        pixels > kMaxDecodedImagePixels - state->image_pixels) {
      state->error = "Raster fallback decoded image pixel budget exceeded";
      return false;
    }
    state->image_pixels += pixels;
    return true;
  }
  if (image.IsTextureBacked() || image.IsDeferredPaintRecord()) {
    DeferUnsupportedPaint(
        state, "Texture-backed or deferred image requires command support");
  }
  if (image.animation_type() == cc::PaintImage::AnimationType::kVideo) {
    DeferUnsupportedPaint(state, "Video image requires original media transport");
  }
  // Animation decoding and color conversion belong to the client. Multipart
  // updates retain the current part's original bytes, checked by serialization.
  if (!state->budget_only && !image.IsTextureBacked() &&
      !image.IsDeferredPaintRecord() &&
      image.animation_type() != cc::PaintImage::AnimationType::kVideo) {
    const auto original = image.GetSwSkImage();
    const auto encoded = original ? original->refEncodedData() : nullptr;
    if (original && (!encoded || !encoded->size())) {
      static base::NoDestructor<std::set<uint32_t>> diagnostics;
      const char* timing = getenv("REACH_DRAWING_TIMING");
      if (diagnostics->size() < 16 && timing &&
          std::string_view(timing) == "1" &&
          diagnostics->insert(original->uniqueID()).second) {
        fprintf(stderr,
                "ReachUnencodedPaintImage image=%u stable=%lld width=%d "
                "height=%d lazy=%d record=%d header=%d animation=%d\n",
                original->uniqueID(), static_cast<long long>(image.stable_id()),
                image.width(), image.height(), image.IsLazyGenerated(),
                image.paint_record() != nullptr,
                image.GetImageHeaderMetadata() != nullptr,
                static_cast<int>(image.animation_type()));
      }
    }
    if (encoded && encoded->size() > 0) {
      if (encoded->size() > kMaxPaintBytes) {
        state->error = "Aggregate original resource budget exceeded";
        return false;
      }
      const auto digest = ImageDigest(original.get(), *encoded);
      const bool loading = image.completion_state() ==
                           cc::PaintImage::CompletionState::kPartiallyDone;
      const auto value = std::make_pair(digest.size, loading);
      const auto [entry, inserted] =
          state->all_image_states.emplace(digest.hash, value);
      if (!inserted && entry->second != value) {
        state->error = "Conflicting original image completion metadata";
        return false;
      }
      if (inserted) {
        if (digest.size > kMaxPaintBytes - state->image_metadata_bytes) {
          state->error = "Aggregate original resource budget exceeded";
          return false;
        }
        state->image_metadata_bytes += digest.size;
      }
      state->image_states.emplace(digest.hash, value);
    }
  }
  if (state->budget_only) {
    const uint64_t pixels = uint64_t(std::max(0, image.width())) *
                            std::max(0, image.height());
    if (!pixels || image.width() > 4096 || image.height() > 4096) {
      state->error = "Decoded image pixel budget exceeded";
      return false;
    }
    if (image.IsTextureBacked() || image.IsDeferredPaintRecord() ||
        image.animation_type() == cc::PaintImage::AnimationType::kVideo) {
      // These remain rejected; do not invoke GetSwSkImage's GPU readback path.
      state->uninspectable_originals = true;
      if (pixels > kMaxDecodedImagePixels - state->image_pixels) {
        state->error = "Decoded image pixel budget exceeded";
        return false;
      }
      state->image_pixels += pixels;
      return true;
    }
    // For a verified non-texture image this returns the cached SkImage, without
    // decoding pixels or executing the image/filter/paint-record effects.
    const auto original = image.GetSwSkImage();
    if (!original) {
      state->error = "Original image resource unavailable";
      return false;
    }
    if (state->budget_images.insert(original->uniqueID()).second) {
      SerializeOriginalImage(original.get(), state);
    }
    return state->error.empty();
  }
  return true;
}

bool DrawImageStates(SkCanvas* canvas, const ImageStates& states,
                     std::string* error) {
  base::ListValue entries;
  for (const auto& [hash, state] : states) {
    base::DictValue value;
    value.Set("hash", sk_reach::Hex(hash));
    value.Set("length", static_cast<int>(state.first));
    value.Set("loading", state.second);
    entries.Append(std::move(value));
    if (entries.size() == 32 || hash == states.rbegin()->first) {
      auto json_entries = base::WriteJson(entries);
      if (!json_entries) {
        *error = "Original image completion metadata serialization failed";
        return false;
      }
      // Like damage metadata, keep the discriminator first on the shared channel.
      const auto json =
          "{\"originalImages\":1,\"entries\":" + *json_entries + "}";
      if (json.size() > 4096) {
        *error = "Original image completion metadata serialization failed";
        return false;
      }
      const auto data = SkData::MakeWithCopy(json.data(), json.size());
      canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.MediaState.v1",
                             data.get());
      entries = base::ListValue();
    }
  }
  return true;
}

sk_sp<SkPicture> WithImageStates(sk_sp<SkPicture> picture,
                                const ImageStates& states,
                                std::string* error) {
  if (!picture || states.empty()) return picture;
  SkPictureRecorder recorder;
  auto* canvas = recorder.beginRecording(picture->cullRect());
  if (!DrawImageStates(canvas, states, error)) return nullptr;
  picture->playback(canvas);
  return recorder.finishRecordingAsPicture();
}

bool CheckPaintRecord(PaintSerialization* state,
                      const cc::PaintRecord& record,
                      size_t depth);

bool CheckPaintShader(PaintSerialization* state,
                      const cc::PaintShader& shader,
                      size_t depth) {
  if (!shader.IsValid()) {
    state->error = "Invalid paint shader";
    return false;
  }
  if (state->raster || state->budget_only) {
    switch (shader.shader_type()) {
      case cc::PaintShader::Type::kEmpty:
      case cc::PaintShader::Type::kColor:
      case cc::PaintShader::Type::kLinearGradient:
      case cc::PaintShader::Type::kRadialGradient:
      case cc::PaintShader::Type::kTwoPointConicalGradient:
      case cc::PaintShader::Type::kSweepGradient:
      case cc::PaintShader::Type::kImage:
      case cc::PaintShader::Type::kPaintRecord:
        break;
      default:
        if (state->budget_only) {
          state->uninspectable_originals = true;
        } else {
          DeferUnsupportedPaint(
              state, "Raster fallback runtime shader is not resource-safe");
        }
        break;
    }
  }
  if (shader.shader_type() == cc::PaintShader::Type::kPaintRecord) {
    const auto* record = shader.paint_record();
    const auto tile = shader.tile();
    if (!record || !tile.isFinite() || tile.isEmpty() ||
        tile.width() > 4096 || tile.height() > 4096) {
      state->error = "Paint-record shader tile is invalid or oversized";
      return false;
    }
    if (!CheckPaintRecord(state, *record, depth + 1)) {
      return false;
    }
  }
  return shader.shader_type() != cc::PaintShader::Type::kImage ||
         CheckPaintImage(state, shader.paint_image());
}

bool CheckPaintFilter(PaintSerialization* state,
                      const cc::PaintFilter* filter,
                      size_t depth = 0,
                      size_t record_depth = 0) {
  if (!state->error.empty()) {
    return false;
  }
  if (!filter) {
    return true;
  }
  if (depth >= kMaxFilterDepth) {
    state->error = "Image-filter graph depth exceeded";
    return false;
  }
  if (++state->filter_visits > kMaxFilterVisits) {
    state->error = "Image-filter graph traversal limit exceeded";
    return false;
  }
  const auto sigma_ok = [](float sigma) {
    return std::isfinite(sigma) && sigma >= 0;
  };
  const auto unsupported = [&]() {
    DeferUnsupportedPaint(
        state, (state->raster ? "Raster fallback Image-filter " : "Image-filter ") +
                   cc::PaintFilter::TypeToString(filter->type()) +
                   (state->raster ? " is not resource-safe"
                                  : " requires command support"));
  };
  if (const auto* crop = filter->GetCropRect(); crop && !crop->isFinite()) {
    state->error = "Image-filter crop is invalid";
    return false;
  }
  switch (filter->type()) {
    case cc::PaintFilter::Type::kBlur: {
      const auto* blur = static_cast<const cc::BlurPaintFilter*>(filter);
      if (!sigma_ok(blur->sigma_x()) || !sigma_ok(blur->sigma_y())) {
        state->error = "Image-filter blur is invalid";
        return false;
      }
      break;
    }
    case cc::PaintFilter::Type::kDropShadow: {
      const auto* shadow =
          static_cast<const cc::DropShadowPaintFilter*>(filter);
      if (!sigma_ok(shadow->sigma_x()) || !sigma_ok(shadow->sigma_y()) ||
          !std::isfinite(shadow->dx()) || !std::isfinite(shadow->dy())) {
        state->error = "Image-filter shadow is invalid";
        return false;
      }
      break;
    }
    case cc::PaintFilter::Type::kOffset: {
      const auto* offset = static_cast<const cc::OffsetPaintFilter*>(filter);
      if (!std::isfinite(offset->dx()) || !std::isfinite(offset->dy())) {
        state->error = "Image-filter offset is invalid";
        return false;
      }
      break;
    }
    case cc::PaintFilter::Type::kMorphology: {
      const auto* morphology =
          static_cast<const cc::MorphologyPaintFilter*>(filter);
      if (!sigma_ok(morphology->radius_x()) ||
          !sigma_ok(morphology->radius_y()) ||
          (morphology->morph_type() !=
               cc::MorphologyPaintFilter::MorphType::kDilate &&
           morphology->morph_type() !=
               cc::MorphologyPaintFilter::MorphType::kErode)) {
        state->error = "Image-filter morphology is invalid";
        return false;
      }
      // Native raster is bounded here. Do not expose an unqualified serialized
      // filter to the frozen client, or stop validation before its descendants.
      if (!state->raster) {
        state->filter_requires_raster =
            "Image-filter kMorphology requires command support";
      }
      break;
    }
    case cc::PaintFilter::Type::kColorFilter:
      break;
    case cc::PaintFilter::Type::kMerge: {
      const auto* merge = static_cast<const cc::MergePaintFilter*>(filter);
      // Charge null inputs too; they do not recurse into a visit.
      if (merge->input_count() > kMaxFilterVisits - state->filter_visits) {
        state->error = "Image-filter graph traversal limit exceeded";
        return false;
      }
      state->filter_visits += merge->input_count();
      for (size_t i = 0; i < merge->input_count(); ++i) {
        if (!CheckPaintFilter(state, merge->input_at(i), depth + 1,
                              record_depth)) {
          return false;
        }
      }
      return true;
    }
    case cc::PaintFilter::Type::kXfermode: {
      const auto* blend =
          static_cast<const cc::XfermodePaintFilter*>(filter);
      return CheckPaintFilter(state, blend->background().get(), depth + 1,
                              record_depth) &&
             CheckPaintFilter(state, blend->foreground().get(), depth + 1,
                              record_depth);
    }
    case cc::PaintFilter::Type::kCompose: {
      const auto* compose =
          static_cast<const cc::ComposePaintFilter*>(filter);
      return CheckPaintFilter(state, compose->outer().get(), depth + 1,
                              record_depth) &&
             CheckPaintFilter(state, compose->inner().get(), depth + 1,
                              record_depth);
    }
    // Inspect only the original cc inputs declared by these exact classes;
    // traversal does not grant command or raster support for their effects.
    case cc::PaintFilter::Type::kMagnifier:
    case cc::PaintFilter::Type::kAlphaThreshold:
    case cc::PaintFilter::Type::kMatrixConvolution:
    case cc::PaintFilter::Type::kTile:
    case cc::PaintFilter::Type::kMatrix:
    case cc::PaintFilter::Type::kLightingDistant:
    case cc::PaintFilter::Type::kLightingPoint:
    case cc::PaintFilter::Type::kLightingSpot:
      unsupported();
      break;
    case cc::PaintFilter::Type::kArithmetic: {
      unsupported();
      const auto* arithmetic =
          static_cast<const cc::ArithmeticPaintFilter*>(filter);
      return CheckPaintFilter(state, arithmetic->background().get(), depth + 1,
                              record_depth) &&
             CheckPaintFilter(state, arithmetic->foreground().get(), depth + 1,
                              record_depth);
    }
    case cc::PaintFilter::Type::kDisplacementMapEffect: {
      unsupported();
      const auto* displacement =
          static_cast<const cc::DisplacementMapEffectPaintFilter*>(filter);
      return CheckPaintFilter(state, displacement->displacement().get(),
                              depth + 1, record_depth) &&
             CheckPaintFilter(state, displacement->color().get(), depth + 1,
                              record_depth);
    }
    case cc::PaintFilter::Type::kImage:
      unsupported();
      return CheckPaintImage(
          state, static_cast<const cc::ImagePaintFilter*>(filter)->image());
    case cc::PaintFilter::Type::kPaintRecord:
      unsupported();
      return CheckPaintRecord(
          state, static_cast<const cc::RecordPaintFilter*>(filter)->record(),
          record_depth + 1);
    case cc::PaintFilter::Type::kShader:
      unsupported();
      return CheckPaintShader(
          state, static_cast<const cc::ShaderPaintFilter*>(filter)->shader(),
          record_depth);
    case cc::PaintFilter::Type::kTurbulence:
      unsupported();
      return true;
    default:
      // Unknown/non-null kNullFilter objects have no verified input API.
      // Fail closed without a capability-only fallback error or guessed cast.
      state->uninspectable_filter = true;
      return true;
  }
  return CheckPaintFilter(
      state, static_cast<const cc::OneInputPaintFilter*>(filter)->input().get(),
      depth + 1, record_depth);
}

const cc::PaintOp* CheckPaintOperation(PaintSerialization* state,
                                      const cc::PaintOp& operation,
                                      size_t depth) {
  if (!state->error.empty()) {
    return nullptr;
  }
  if (operation.GetType() == cc::PaintOpType::kCustomData) {
    const auto id = static_cast<const cc::CustomDataOp&>(operation).id;
    if (state->collect_paint_ids) {
      state->media_ids.insert(id);
    } else if (!state->media_ids.contains(id)) {
      DeferUnsupportedPaint(
          state, "Subframe/compositor transaction requires command support");
    }
  }
  if (operation.GetType() == cc::PaintOpType::kDrawRecord &&
      !CheckPaintRecord(
          state, static_cast<const cc::DrawRecordOp&>(operation).record,
          depth + 1)) {
    return nullptr;
  }
  if (operation.GetType() == cc::PaintOpType::kDrawSkottie ||
      operation.GetType() == cc::PaintOpType::kDrawSlug) {
    DeferUnsupportedPaint(state, "Skottie or GPU text requires command support");
    if (state->budget_only) state->uninspectable_originals = true;
  }
  if (state->budget_only &&
      operation.GetType() == cc::PaintOpType::kDrawTextBlob) {
    const auto& blob = static_cast<const cc::DrawTextBlobOp&>(operation).blob;
    if (blob) {
      SkTextBlob::Iter iterator(*blob);
      SkTextBlob::Iter::Run run;
      while (iterator.next(&run)) {
        if (run.fTypeface &&
            state->budget_typefaces.insert(run.fTypeface->uniqueID()).second) {
          SerializeOriginalTypeface(run.fTypeface, state);
          if (!state->error.empty()) return nullptr;
        }
      }
    }
  }
  if ((operation.GetType() == cc::PaintOpType::kDrawImage &&
       !CheckPaintImage(
           state, static_cast<const cc::DrawImageOp&>(operation).image)) ||
      (operation.GetType() == cc::PaintOpType::kDrawImageRect &&
       !CheckPaintImage(
           state, static_cast<const cc::DrawImageRectOp&>(operation).image))) {
    return nullptr;
  }
  if (operation.GetType() == cc::PaintOpType::kSaveLayerFilters) {
    const auto& layer = static_cast<const cc::SaveLayerFiltersOp&>(operation);
    if (!CheckPaintFilter(state, layer.backdrop_filter.get(), 0, depth)) {
      return nullptr;
    }
    for (const auto& filter : layer.filters) {
      if (!CheckPaintFilter(state, filter.get(), 0, depth)) {
        return nullptr;
      }
    }
  }
  if (operation.IsPaintOpWithFlags()) {
    const auto& flags =
        static_cast<const cc::PaintOpWithFlags&>(operation).flags;
    if (!CheckPaintFilter(state, flags.getImageFilter().get(), 0, depth)) {
      return nullptr;
    }
    if (const auto* shader = flags.getShader()) {
      if (!CheckPaintShader(state, *shader, depth)) {
        return nullptr;
      }
    }
  }
  return &operation;
}

bool CheckPaintRecord(PaintSerialization* state,
                      const cc::PaintRecord& record,
                      size_t depth) {
  if (!state->error.empty()) {
    return false;
  }
  if (depth >= kMaxRecordDepth ||
      record.bytes_used() > kMaxPaintBytes - state->record_bytes) {
    state->error = "Nested paint-record depth or byte budget exceeded";
    return false;
  }
  state->record_bytes += record.bytes_used();
  for (const auto& operation : record) {
    if (!CheckPaintOperation(state, operation, depth)) {
      return false;
    }
  }
  if (depth == 0) {
    if (state->budget_only) {
      if (state->uninspectable_originals) {
        state->error =
            "Original resource budget cannot inspect unsupported payload";
        return false;
      }
      return true;
    }
    if (!state->raster &&
        (state->uninspectable_filter || !state->unsupported_error.empty() ||
         !state->filter_requires_raster.empty())) {
      // Capability errors cannot hide budgets normally enforced during
      // serialization. Inspect originals without recording/rendering rejected
      // effects, emitting resources, or counting them in the export state.
      PaintSerialization budget;
      budget.budget_only = true;
      budget.collect_paint_ids = state->collect_paint_ids;
      budget.media_ids = state->media_ids;
      if (!CheckPaintRecord(&budget, record, 0)) {
        state->error = budget.error;
        return false;
      }
    }
    if (state->uninspectable_filter) {
      state->error = "Uninspectable image-filter category";
      return false;
    }
    if (!state->unsupported_error.empty()) {
      state->error = state->unsupported_error;
      return false;
    }
    if (!state->filter_requires_raster.empty()) {
      state->error = state->filter_requires_raster;
      return false;
    }
  }
  return true;
}

// Same-process child frames paint inline into their local root, as in normal
// Chromium paint. Only remote frames and plugins are composed by identity.
bool IsInlinePaintFrame(blink::LocalFrame* root, blink::Frame* frame) {
  auto* local = blink::DynamicTo<blink::LocalFrame>(frame);
  return local && &local->LocalFrameRoot() == &root->LocalFrameRoot();
}

// Remote child frames whose owner element paints inside |root|'s output.
bool IsComposedChildFrame(blink::LocalFrame* root, blink::Frame* frame) {
  return !blink::IsA<blink::LocalFrame>(frame) &&
         IsInlinePaintFrame(root, frame->Tree().Parent());
}

// Captures read the normal lifecycle paint artifact, so the local root must be
// paint-clean. Post-lifecycle steps such as IntersectionObserver delivery can
// dirty it again; a following lifecycle update settles that work.
bool EnsurePaintClean(blink::LocalFrame* frame) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (!frame->IsAttached() || !frame->View() || !frame->GetPage() ||
        !frame->GetDocument()) {
      return false;
    }
    if (frame->GetDocument()->Lifecycle().GetState() ==
        blink::DocumentLifecycle::kPaintClean) {
      return true;
    }
    frame->GetPage()->Animator().UpdateAllLifecyclePhases(
        *frame, blink::DocumentUpdateReason::kInspector);
  }
  return frame->IsAttached() && frame->View() && frame->GetDocument() &&
         frame->GetDocument()->Lifecycle().GetState() ==
             blink::DocumentLifecycle::kPaintClean;
}

gfx::Rect CommandPaintCull(const gfx::Rect& bounds) {
  return gfx::Rect(0, -bounds.height(), bounds.width(), 3 * bounds.height());
}

// Dependency discovery and chunk export must select the same display items.
// The hook deliberately retains clipped chunks for client scroll prefetch.
cc::PaintRecord ConvertCommandPaint(
    blink::LocalFrame* frame, const gfx::Rect& cull,
    blink::PaintChunksToCcLayer::ChunkContentHook content) {
  return blink::PaintChunksToCcLayer::Convert(
      blink::PaintChunkSubset(frame->View()->GetPaintArtifact()),
      blink::PropertyTreeState::Root(), &cull, content);
}

std::optional<cc::PaintRecord> CaptureRequiredPaint(
    blink::WebLocalFrame* web_frame, const gfx::Rect& bounds,
    PaintSerialization* dependencies) {
  if (bounds.IsEmpty() || bounds.width() > 4096 || bounds.height() > 4096 ||
      static_cast<uint64_t>(bounds.width()) * bounds.height() >
          kMaxViewportPixels) {
    dependencies->error = "Capture viewport is empty or exceeds budget";
    return std::nullopt;
  }
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame->IsLocalRoot()) {
    // Painted inline by its local root; it has no separate picture.
    return cc::PaintRecord();
  }
  if (!dependencies->raster && !EnsurePaintClean(frame)) {
    dependencies->error = "Frame lifecycle did not reach paint-clean";
    return std::nullopt;
  }
  if (!frame->GetDocument() || !frame->GetDocument()->GetLayoutView()) {
    dependencies->error = "Frame changed during lifecycle update";
    return std::nullopt;
  }
  std::set<uint32_t> frame_ids;
  uint64_t webgl_pixels = 0;
  for (blink::Frame* tree_frame = frame; tree_frame;
       tree_frame = tree_frame->Tree().TraverseNext(frame)) {
    if (!IsInlinePaintFrame(frame, tree_frame)) {
      continue;
    }
    auto* tree_document =
        blink::To<blink::LocalFrame>(tree_frame)->GetDocument();
    auto* root = tree_document ? tree_document->GetLayoutView() : nullptr;
    for (blink::LayoutObject* object = root; object;
         object = object->NextInPreOrder(root)) {
      if (object->IsLayoutEmbeddedContent()) {
        if (auto* owner = blink::DynamicTo<blink::HTMLFrameOwnerElement>(
                object->GetNode())) {
          frame_ids.insert(owner->GetDomNodeId());
        }
      }
      const auto* element =
          blink::DynamicTo<blink::HTMLCanvasElement>(object->GetNode());
      if (!element) {
        continue;
      }
      auto* context = element->RenderingContext();
      if (!dependencies->raster &&
          blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode() &&
          element->IsOffscreenCanvasRegistered()) {
        dependencies->error = "Worker OffscreenCanvas video is unsupported";
        return std::nullopt;
      }
      if (context && context->IsWebGL()) {
        if (!dependencies->raster &&
            blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode()) {
          if (web_frame->Parent()) {
            dependencies->error = "Out-of-process WebGL video is unsupported";
            return std::nullopt;
          }
          if (context->isContextLost())
            continue;
          if (!context->CheckpointReachCanvasVideo()) {
            const auto* snapshot = context->GetReachCanvasVideo();
            dependencies->error = snapshot ? snapshot->error()
                                           : "WebGL video snapshot unavailable";
            return std::nullopt;
          }
          continue;
        }
        const auto size = element->Size();
        const uint64_t pixels =
            uint64_t(std::max(0, size.width())) * std::max(0, size.height());
        // Blink's ordinary canvas paint makes an unaccelerated snapshot.
        // Bound its source allocation before recording that raster paint.
        if (!pixels || size.width() > 4096 || size.height() > 4096 ||
            pixels > kMaxDecodedImagePixels - webgl_pixels) {
          dependencies->error = "WebGL canvas pixel budget exceeded";
          return std::nullopt;
        }
        webgl_pixels += pixels;
        if (context->isContextLost()) {
          dependencies->error = "WebGL canvas context is lost";
          return std::nullopt;
        }
        if (!dependencies->raster &&
            blink::Canvas2DBitmapProvider::IsReachWebGLCommandMode()) {
          if (web_frame->Parent()) {
            dependencies->error =
                "Out-of-process WebGL canvas requires owned journal routing";
            return std::nullopt;
          }
          auto* journal = context->GetReachWebGLJournal();
          if (!journal || !journal->usable()) {
            dependencies->error =
                journal ? "WebGL command transport: " + journal->error()
                        : "WebGL canvas has no command transport";
            return std::nullopt;
          }
          continue;
        }
      }
      if (element->IsOffscreenCanvasRegistered() ||
          (context && !context->IsRenderingContext2D() &&
           !(dependencies->raster && context->IsWebGL()))) {
        dependencies->error =
            "GPU, bitmap or offscreen canvas requires command transport";
        return std::nullopt;
      }
      if (dependencies->raster) {
        continue;
      }
      if (context) {
        if (!blink::Canvas2DBitmapProvider::IsReachCommandMode()) {
          dependencies->error = "Canvas2D requires a command-only runtime";
          return std::nullopt;
        }
        context->FlushCanvas(blink::FlushReason::kPrinting);
        if (context->IsPaintable() && !context->GetLastRecording()) {
          dependencies->error =
              "Canvas2D readback, raw pixels or command budget requires "
              "support";
          return std::nullopt;
        }
      }
    }
  }
  cc::PaintRecord recording;
  if (dependencies->raster) {
    // Server raster fallback paints software video frames, so it records a
    // separate out-of-lifecycle paint without command-mode media markers.
    cc::PaintRecorder recorder;
    auto* canvas = recorder.beginRecording();
    canvas->translate(-bounds.x(), -bounds.y());
    const bool previous = blink::SetReachMediaCapture(false);
    base::ScopedClosureRunner restore(base::BindOnce(
        [](bool value) { blink::SetReachMediaCapture(value); }, previous));
    blink::PaintRecordBuilder builder;
    frame->View()->PaintOutsideOfLifecycle(
        builder.Context(), blink::PaintFlag::kOmitCompositingInfo,
        blink::CullRect(bounds));
    auto* layout_view =
        frame->GetDocument() ? frame->GetDocument()->GetLayoutView() : nullptr;
    if (!frame->IsAttached() || !layout_view) {
      dependencies->error = "Blink paint recording failed";
      return std::nullopt;
    }
    canvas->drawPicture(builder.EndRecording(
        layout_view->FirstFragment().ContentsProperties().Unalias()));
    recording = recorder.finishRecordingAsPicture();
  } else {
    recording = ConvertCommandPaint(
        frame, CommandPaintCull(bounds),
        [](const blink::PaintChunk&, cc::PaintRecord items) { return items; });
  }
  dependencies->collect_paint_ids = true;
  if (!CheckPaintRecord(dependencies, recording, 0)) {
    return std::nullopt;
  }
  // Chunk contents are serialized without the skeleton's viewport clips.
  // Their child/video markers must be composed even while prefetched. Only
  // raster capture can prove an iframe unnecessary using viewport rejection.
  if (!dependencies->raster) {
    return recording;
  }
  std::set<uint32_t> visible_ids;
  cc::PlaybackCallbacks callbacks;
  // EmbeddedContentPainter transforms each iframe marker to a unit rectangle.
  // Vector recording preserves those transforms/clips without rasterizing.
  // Unknown custom operations are never culled by this iframe-only proof.
  callbacks.custom_callback = base::BindRepeating(
      [](PaintSerialization* dependencies, const std::set<uint32_t>* frames,
         std::set<uint32_t>* visible,
         SkCanvas* canvas, uint32_t id) {
        if (!frames->contains(id)) {
          return;
        }
        if (canvas->quickReject(SkRect::MakeWH(1, 1))) {
          dependencies->culled_paint_ids.insert(id);
        } else {
          visible->insert(id);
        }
      }, base::Unretained(dependencies), base::Unretained(&frame_ids),
      base::Unretained(&visible_ids));
  auto dependency_picture = recording.ToSkPicture(
      SkRect::MakeWH(bounds.width(), bounds.height()), nullptr, callbacks);
  if (!dependency_picture) {
    dependencies->error = "Paint dependency recording failed";
    return std::nullopt;
  }
  for (const auto id : visible_ids) {
    dependencies->culled_paint_ids.erase(id);
  }
  for (const auto id : dependencies->culled_paint_ids) {
    dependencies->media_ids.erase(id);
  }
  return recording;
}

blink::ExecutionContext* GetExecutionContext(v8::Local<v8::Context> context) {
  blink::LocalFrame* frame = blink::ToLocalFrameIfNotDetached(context);
  if (frame &&
      frame->DomWindow()->CanExecuteScripts(blink::kAboutToExecuteScript)) {
    return frame->GetDocument()->GetExecutionContext();
  }
  return nullptr;
}

}  // namespace

bool CanGoBack(blink::WebView* view) {
  if (!view) {
    return false;
  }
  return view->HistoryBackListCount() > 0;
}

bool CanGoForward(blink::WebView* view) {
  if (!view) {
    return false;
  }
  return view->HistoryForwardListCount() > 0;
}

void GoBack(blink::WebView* view) {
  if (!view) {
    return;
  }

  blink::WebFrame* main_frame = view->MainFrame();
  if (main_frame && main_frame->IsWebLocalFrame()) {
    if (view->HistoryBackListCount() > 0) {
      blink::Frame* core_frame = blink::WebFrame::ToCoreFrame(*main_frame);
      blink::To<blink::LocalFrame>(core_frame)
          ->GetLocalFrameHostRemote()
          .GoToEntryAtOffset(-1, /*has_user_gesture=*/true,
                             base::TimeTicks::Now(), std::nullopt);
    }
  }
}

void GoForward(blink::WebView* view) {
  if (!view) {
    return;
  }

  blink::WebFrame* main_frame = view->MainFrame();
  if (main_frame && main_frame->IsWebLocalFrame()) {
    if (view->HistoryForwardListCount() > 0) {
      blink::Frame* core_frame = blink::WebFrame::ToCoreFrame(*main_frame);
      blink::To<blink::LocalFrame>(core_frame)
          ->GetLocalFrameHostRemote()
          .GoToEntryAtOffset(1, /*has_user_gesture=*/true,
                             base::TimeTicks::Now(), std::nullopt);
    }
  }
}

bool IsInBackForwardCache(blink::WebLocalFrame* frame) {
  blink::Frame* core_frame = blink::WebFrame::ToCoreFrame(*frame);
  return blink::To<blink::LocalFrame>(core_frame)
      ->GetPage()
      ->GetPageScheduler()
      ->IsInBackForwardCache();
}

blink::WebString DumpDocumentText(blink::WebLocalFrame* frame) {
  // We use the document element's text instead of the body text here because
  // not all documents have a body, such as XML documents.
  blink::WebElement document_element = frame->GetDocument().DocumentElement();
  if (document_element.IsNull()) {
    return blink::WebString();
  }

  blink::Element* web_element = document_element.Unwrap<blink::Element>();
  return blink::WebString(web_element->innerText());
}

blink::WebString DumpDocumentMarkup(blink::WebLocalFrame* frame) {
  return blink::CreateMarkup(
      blink::To<blink::WebLocalFrameImpl>(frame)->GetFrame()->GetDocument());
}

namespace {
int DrawingInputFlags(int modifiers) {
  int flags = blink::WebInputEvent::kNoModifiers;
  if (modifiers & EVENTFLAG_SHIFT_DOWN) flags |= blink::WebInputEvent::kShiftKey;
  if (modifiers & EVENTFLAG_CONTROL_DOWN) flags |= blink::WebInputEvent::kControlKey;
  if (modifiers & EVENTFLAG_ALT_DOWN) flags |= blink::WebInputEvent::kAltKey;
  if (modifiers & EVENTFLAG_COMMAND_DOWN) flags |= blink::WebInputEvent::kMetaKey;
  if (modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON) flags |= blink::WebInputEvent::kLeftButtonDown;
  if (modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON) flags |= blink::WebInputEvent::kMiddleButtonDown;
  if (modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON) flags |= blink::WebInputEvent::kRightButtonDown;
  if (modifiers & EVENTFLAG_CAPS_LOCK_ON) flags |= blink::WebInputEvent::kCapsLockOn;
  if (modifiers & EVENTFLAG_NUM_LOCK_ON) flags |= blink::WebInputEvent::kNumLockOn;
  if (modifiers & EVENTFLAG_IS_LEFT) flags |= blink::WebInputEvent::kIsLeft;
  if (modifiers & EVENTFLAG_IS_RIGHT) flags |= blink::WebInputEvent::kIsRight;
  if (modifiers & EVENTFLAG_IS_KEY_PAD) flags |= blink::WebInputEvent::kIsKeyPad;
  return flags;
}
}  // namespace

std::string ApplyDrawingKey(blink::WebLocalFrame* web_frame,
                           int kind, int windows, int native, int modifiers,
                           int character, int unmodified) {
  blink::Persistent<blink::LocalFrame> frame =
      blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetDocument() || !frame->View())
    return "Keyboard frame is detached or not ready";
  const auto type = kind == 0 ? blink::WebInputEvent::Type::kRawKeyDown :
      kind == 1 ? blink::WebInputEvent::Type::kKeyUp : blink::WebInputEvent::Type::kChar;
  blink::WebKeyboardEvent event(type, DrawingInputFlags(modifiers), base::TimeTicks::Now());
  event.windows_key_code = windows;
  event.native_key_code = native;
  const auto code = ui::KeycodeConverter::NativeKeycodeToDomCode(native);
  event.dom_code = static_cast<int>(code);
  ui::DomKey key;
  ui::KeyboardCode mapped;
  if (!ui::DomCodeToNonPrintableDomKey(code, &key, &mapped))
    key = ui::DomKey::FromCharacter(character >= 0x20 ? character : unmodified);
  event.dom_key = key;
  event.text[0] = static_cast<char16_t>(character);
  event.unmodified_text[0] = static_cast<char16_t>(unmodified);
  frame->GetEventHandler().KeyEvent(event);
  return {};
}

std::string ApplyDrawingMouse(blink::WebLocalFrame* web_frame,
                             int kind, int x, int y, int modifiers,
                             int button, int up, int count) {
  blink::Persistent<blink::LocalFrame> frame =
      blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetDocument() ||
      !frame->GetPage() || !frame->View())
    return "Mouse frame is detached or not ready";
  blink::Persistent<blink::Document> document = frame->GetDocument();
  frame->GetPage()->Animator().UpdateAllLifecyclePhasesExceptPaint(
      *frame, blink::DocumentUpdateReason::kInspector);
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      !frame->View())
    return "Frame changed during mouse layout";
  const auto point = frame->View()->ConvertToRootFrame(gfx::Point(x, y));
  x = point.x();
  y = point.y();
  auto mouse_button = blink::WebPointerProperties::Button::kNoButton;
  if (kind == 2) {
    mouse_button = button == 0 ? blink::WebPointerProperties::Button::kLeft :
        button == 1 ? blink::WebPointerProperties::Button::kMiddle :
                      blink::WebPointerProperties::Button::kRight;
    const int flag = button == 0 ? EVENTFLAG_LEFT_MOUSE_BUTTON :
        button == 1 ? EVENTFLAG_MIDDLE_MOUSE_BUTTON : EVENTFLAG_RIGHT_MOUSE_BUTTON;
    // CEF supplies pre-transition flags; Blink consumes post-transition flags.
    modifiers = up ? (modifiers & ~flag) : (modifiers | flag);
  } else if (kind == 0) {
    if (modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON)
      mouse_button = blink::WebPointerProperties::Button::kLeft;
    else if (modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON)
      mouse_button = blink::WebPointerProperties::Button::kMiddle;
    else if (modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON)
      mouse_button = blink::WebPointerProperties::Button::kRight;
  }
  const auto type = kind == 0 ? blink::WebInputEvent::Type::kMouseMove :
      kind == 1 ? blink::WebInputEvent::Type::kMouseLeave :
      up ? blink::WebInputEvent::Type::kMouseUp : blink::WebInputEvent::Type::kMouseDown;
  blink::WebMouseEvent event(type, DrawingInputFlags(modifiers), base::TimeTicks::Now());
  event.SetPositionInWidget(x, y);
  event.SetPositionInScreen(x, y);
  event.button = mouse_button;
  event.click_count = count;
  blink::Persistent<blink::Page> page = frame->GetPage();
  bool context_menu = kind == 2 &&
      mouse_button == blink::WebPointerProperties::Button::kRight;
#if BUILDFLAG(IS_MAC)
  if (kind == 2 && !up &&
      mouse_button == blink::WebPointerProperties::Button::kLeft &&
      (event.GetModifiers() & blink::WebInputEvent::kControlKey)) {
    context_menu = true;
  }
#endif
  context_menu = context_menu &&
      (static_cast<bool>(up) ==
       page->GetSettings().GetShowContextMenuOnMouseUp());
  const auto frame_unchanged = [&]() {
    return frame->IsAttached() && frame->GetDocument() == document.Get() &&
           frame->GetPage() == page.Get() && frame->View();
  };
  const auto menu_frame_at_point =
      [&](bool no_lifecycle_update) -> blink::LocalFrame* {
    blink::HitTestLocation location(blink::PhysicalOffset(
        frame->View()->ConvertFromRootFrame(gfx::Point(x, y))));
    const auto hit = frame->GetEventHandler().HitTestResultAtLocation(
        location,
        blink::HitTestRequest::kReadOnly | blink::HitTestRequest::kActive,
        nullptr, no_lifecycle_update);
    blink::Frame* target = hit.InnerNodeOrImageMapImage()
        ? hit.InnerNodeOrImageMapImage()->GetDocument().GetFrame()
        : page->GetFocusController().FocusedOrMainFrame();
    return blink::DynamicTo<blink::LocalFrame>(target);
  };
  // Mouse dispatch can replace a child document as well as the input frame.
  blink::Persistent<blink::LocalFrame> menu_frame;
  blink::Persistent<blink::Document> menu_document;
  if (context_menu) {
    const auto* root_document = frame->LocalFrameRoot().GetDocument();
    if (!root_document || !root_document->IsActive() || !document->IsActive() ||
        root_document->Lifecycle().GetState() <
            blink::DocumentLifecycle::kPrePaintClean ||
        document->Lifecycle().GetState() <
            blink::DocumentLifecycle::kPrePaintClean)
      return "Context menu layout is not ready";
    // Layout is already current; capture child authority without another
    // lifecycle update that could run page callbacks before the capture.
    menu_frame = menu_frame_at_point(true);
    if (!frame_unchanged())
      return "Frame changed during context menu hit test";
    if (menu_frame)
      menu_document = menu_frame->GetDocument();
  }
  if (kind == 0) frame->GetEventHandler().HandleMouseMoveEvent(event, {}, {});
  else if (kind == 1) frame->GetEventHandler().HandleMouseLeaveEvent(event);
  else if (up) frame->GetEventHandler().HandleMouseReleaseEvent(event);
  else frame->GetEventHandler().HandleMousePressEvent(event);
  if (context_menu) {
    const auto menu_frame_unchanged = [&]() {
      return frame_unchanged() && menu_frame && menu_frame->IsAttached() &&
             menu_document && menu_frame->GetDocument() == menu_document.Get() &&
             menu_frame->GetPage() == page.Get() && menu_frame->View();
    };
    if (!menu_frame_unchanged())
      return "Frame changed during context menu mouse dispatch";
    page->GetContextMenuController().ClearContextMenu();
    if (!menu_frame_unchanged())
      return "Frame changed during context menu clearing";
    auto* target = menu_frame_at_point(false);
    if (!menu_frame_unchanged() || target != menu_frame.Get())
      return "Target changed before context menu dispatch";
    event.menu_source_type = ui::mojom::blink::MenuSourceType::kMouse;
    event.id = blink::PointerEventFactory::kMouseId;
    blink::ContextMenuAllowedScope scope;
    menu_frame->GetEventHandler().SendContextMenuEvent(event);
  }
  return {};
}

std::string ApplyDrawingWheel(blink::WebLocalFrame* web_frame,
                             int x, int y, int modifiers, int dx, int dy) {
  blink::Persistent<blink::LocalFrame> frame =
      blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetDocument() ||
      !frame->GetPage() || !frame->View()) {
    return "Wheel frame is detached or not ready";
  }
  blink::Persistent<blink::Document> document = frame->GetDocument();
  frame->GetPage()->Animator().UpdateAllLifecyclePhasesExceptPaint(
      *frame, blink::DocumentUpdateReason::kInspector);
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      !frame->View() || !document->GetLayoutView()) {
    return "Frame changed during wheel layout";
  }
  const auto point = frame->View()->ConvertToRootFrame(gfx::Point(x, y));
  x = point.x();
  y = point.y();
  blink::HitTestLocation location(blink::PhysicalOffset(
      frame->View()->ConvertFromRootFrame(gfx::Point(x, y))));
  blink::HitTestResult hit(blink::HitTestRequest::kReadOnly, location);
  document->GetLayoutView()->HitTest(location, hit);
  blink::Persistent<blink::Node> target = hit.InnerNode();
  if (!target || target->GetDocument() != document.Get()) {
    return "Wheel target is unavailable or embedded";
  }
  size_t areas = 0;
  for (auto& area : blink::ScrollableAreaTraversal(target)) {
    if (++areas > 64) {
      return "Wheel scroll chain exceeds budget";
    }
    if (const auto* box = area.GetLayoutBox();
        box && !box->StyleRef().GetScrollSnapType().is_none) {
      return "Scroll snapping requires command support";
    }
  }
  const int flags = DrawingInputFlags(modifiers);
  blink::WebMouseWheelEvent event(
      blink::WebInputEvent::Type::kMouseWheel, flags, base::TimeTicks::Now());
  event.SetPositionInWidget(x, y);
  event.SetPositionInScreen(x, y);
  event.delta_x = dx;
  event.delta_y = dy;
  // Chromium excludes precise-pixel events from Ctrl+wheel page zoom.
  event.delta_units =
      (flags & blink::WebInputEvent::kControlKey) && dy != 0
          ? ui::ScrollGranularity::kScrollByPixel
          : ui::ScrollGranularity::kScrollByPrecisePixel;
  event.event_action =
      blink::WebMouseWheelEvent::GetPlatformSpecificDefaultEventAction(event);
  if (frame->GetEventHandler().HandleWheelEvent(event) !=
      blink::WebInputEventResult::kNotHandled) {
    return {};
  }
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      !frame->View() || !target->isConnected()) {
    return "Frame or target changed during wheel dispatch";
  }
  if (event.event_action == blink::WebMouseWheelEvent::EventAction::kPageZoom) {
    return "Wheel page zoom requires command support";
  }
  frame->GetPage()->Animator().UpdateAllLifecyclePhasesExceptPaint(
      *frame, blink::DocumentUpdateReason::kInspector);
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      !frame->View() || !target->isConnected()) {
    return "Frame or target changed during wheel update";
  }
  areas = 0;
  for (auto& area : blink::ScrollableAreaTraversal(target)) {
    if (++areas > 64) return "Wheel scroll chain exceeds budget";
    if (const auto* box = area.GetLayoutBox();
        box && !box->StyleRef().GetScrollSnapType().is_none) {
      return "Scroll snapping requires command support";
    }
  }
  blink::ScrollOffset remaining(-dx, -dy);
  if ((flags & blink::WebInputEvent::kShiftKey) && dx == 0) {
    remaining = blink::ScrollOffset(-dy, 0);
  }
  areas = 0;
  for (auto& area : blink::ScrollableAreaTraversal(target)) {
    if (++areas > 64) return "Wheel scroll chain exceeds budget";
    const auto consumed = area.UserScroll(
        ui::ScrollGranularity::kScrollByPrecisePixel, remaining,
        cc::ScrollSourceType::kRelativeScroll, {});
    remaining = blink::ScrollOffset(consumed.unused_scroll_delta_x,
                                    consumed.unused_scroll_delta_y);
    if (const auto* box = area.GetLayoutBox()) {
      const auto& style = box->StyleRef();
      if (style.OverscrollBehaviorX() != blink::EOverscrollBehavior::kAuto)
        remaining.set_x(0);
      if (style.OverscrollBehaviorY() != blink::EOverscrollBehavior::kAuto)
        remaining.set_y(0);
    }
    if (remaining.IsZero()) break;
  }
  return {};
}

bool IsDrawingDocument(blink::WebLocalFrame* web_frame,
                       const std::string& document) {
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  return frame && frame->IsAttached() && frame->GetDocument() &&
         frame->GetDocument()->Token().ToString() == document;
}

// Initial local documents can paint before the browser supplies an embedding
// token. Their document token is shared with the local owner and changes on
// navigation; remote frames must wait for their browser-issued embedding token.
std::string DrawingFrameIdentity(blink::Frame* frame) {
  if (frame->GetEmbeddingToken())
    return frame->GetEmbeddingToken()->ToString();
  auto* local = blink::DynamicTo<blink::LocalFrame>(frame);
  return local && local->GetDocument()
      ? "document:" + local->GetDocument()->Token().ToString() : "";
}

CefDrawingCommands GetDrawingInputRoute(blink::WebLocalFrame* web_frame,
                                        bool keyboard, int x, int y,
                                        const std::function<std::string()>& apply) {
  CefDrawingCommands result;
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->View() ||
      !frame->GetPage() || !frame->GetDocument()) {
    result.error = "Input frame is detached or not ready";
    return result;
  }
  frame->View()->UpdateAllLifecyclePhasesExceptPaint(
      blink::DocumentUpdateReason::kInspector);
  blink::Frame* child = nullptr;
  double route_x = x;
  double route_y = y;
  if (keyboard) {
    child = frame->GetPage()->GetFocusController().FocusedFrameIncludingRemote();
    if (child == frame)
      child = nullptr;
  } else {
    blink::HitTestLocation location(blink::PhysicalOffset(gfx::Point(x, y)));
    blink::HitTestResult hit(blink::HitTestRequest::kReadOnly, location);
    if (frame->GetDocument()->GetLayoutView())
      frame->GetDocument()->GetLayoutView()->HitTest(location, hit);
    auto* owner =
        blink::DynamicTo<blink::HTMLFrameOwnerElement>(hit.InnerNode());
    if (owner && hit.IsOverEmbeddedContentView()) {
      auto* layout =
          blink::DynamicTo<blink::LayoutEmbeddedContent>(owner->GetLayoutObject());
      if (layout && owner->ContentFrame()) {
        const auto rect = layout->ReplacedContentRect();
        const auto point = hit.LocalPoint() - rect.offset;
        if (rect.IsEmpty()) {
          result.error = "Input frame content bounds are empty";
          return result;
        }
        route_x = point.left.ToFloat() / rect.Width().ToFloat();
        route_y = point.top.ToFloat() / rect.Height().ToFloat();
        child = owner->ContentFrame();
      }
    }
  }
  if (child && DrawingFrameIdentity(child).empty()) {
    result.error = "Input frame has no embedding identity";
    return result;
  }
  base::DictValue route;
  route.Set("document", frame->GetDocument()->Token().ToString());
  route.Set("embedding", child ? DrawingFrameIdentity(child) : "");
  route.Set("x", route_x);
  route.Set("y", route_y);
  if (!child && apply) {
    route.Set("applied", true);
    if (const auto error = apply(); !error.empty())
      route.Set("inputError", error);
  }
  auto json = base::WriteJson(route);
  if (!json) {
    result.error = "Input route serialization failed";
    return result;
  }
  result.metadata = std::move(*json);
  return result;
}

CefDrawingCommands GetDrawingFrameInfo(blink::WebLocalFrame* web_frame,
                                      bool raster) {
  CefDrawingCommands result;
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->View() ||
      !frame->GetDocument()) {
    result.error = "Frame is detached or not ready";
    return result;
  }
  frame->View()->UpdateAllLifecyclePhasesExceptPaint(
      blink::DocumentUpdateReason::kInspector);
  const auto bounds = frame->View()->LayoutViewport()->VisibleContentRect(
      blink::kIncludeScrollbars);
  base::DictValue info;
  info.Set("document", frame->GetDocument()->Token().ToString());
  info.Set("embedding", DrawingFrameIdentity(frame));
  info.Set("width", bounds.width());
  info.Set("height", bounds.height());
  PaintSerialization dependencies;
  dependencies.raster = raster;
  // Only composed (remote) child frames need the paint dependency proof; with
  // none, |children| is empty whatever paints, and the capture itself records
  // and validates the paint. Skip that full paint and replay.
  bool composed_children = false;
  for (auto* child = frame->Tree().TraverseNext(frame); child;
       child = child->Tree().TraverseNext(frame)) {
    if (IsComposedChildFrame(frame, child)) {
      composed_children = true;
      break;
    }
  }
  if (!composed_children && !raster) {
    if (bounds.width() > 4096 || bounds.height() > 4096 ||
        static_cast<uint64_t>(bounds.width()) * bounds.height() >
            kMaxViewportPixels) {
      result.error = "Capture viewport is empty or exceeds budget";
      return result;
    }
  } else if (!bounds.IsEmpty() &&
             !CaptureRequiredPaint(web_frame, bounds, &dependencies)) {
    result.error = dependencies.error;
    return result;
  }
  if (!frame->IsAttached() || !frame->GetDocument() ||
      frame->GetDocument()->Token().ToString() !=
          *info.FindString("document")) {
    result.error = "Frame changed during dependency recording";
    return result;
  }
  base::ListValue children;
  for (auto* child = frame->Tree().TraverseNext(frame); child;
       child = child->Tree().TraverseNext(frame)) {
    if (!IsComposedChildFrame(frame, child)) {
      continue;
    }
    const auto* owner = child->OwnerLayoutObject();
    auto* element = owner
        ? blink::DynamicTo<blink::HTMLFrameOwnerElement>(owner->GetNode())
        : nullptr;
    if (!element ||
        !dependencies.media_ids.contains(element->GetDomNodeId()))
      continue;
    if (DrawingFrameIdentity(child).empty()) {
      result.error = "Paint-required child frame has no embedding identity";
      return result;
    }
    children.Append(DrawingFrameIdentity(child));
  }
  info.Set("children", std::move(children));
  auto json = base::WriteJson(info);
  if (!json || json->size() > (raster ? 65536u : 8192u)) {
    result.error = "Frame metadata budget exceeded";
    return result;
  }
  result.metadata = std::move(*json);
  return result;
}

namespace {

// Opt-in (REACH_DRAWING_TIMING=1, inherited by renderers) per-stage timing of
// drawing-command captures. Main thread only.
class CaptureTimer {
 public:
  static CaptureTimer& Get() {
    static base::NoDestructor<CaptureTimer> timer;
    return *timer;
  }
  void Start() {
    if (!Enabled()) return;
    start_ = last_ = base::TimeTicks::Now();
    stages_.clear();
  }
  void Mark(const char* stage) {
    if (start_.is_null()) return;
    const auto now = base::TimeTicks::Now();
    stages_ += std::string(" ") + stage + "=" +
               base::NumberToString((now - last_).InMilliseconds());
    last_ = now;
  }
  void Finish(const std::string& detail) {
    if (start_.is_null()) return;
    const std::string line = "reach drawing timing:" + stages_ + " total=" +
        base::NumberToString((base::TimeTicks::Now() - start_).InMilliseconds()) +
        " " + detail + "\n";
    fputs(line.c_str(), stderr);
    start_ = base::TimeTicks();
  }

 private:
  static bool Enabled() {
    static const bool enabled = [] {
      const char* value = getenv("REACH_DRAWING_TIMING");
      return value && std::string_view(value) == "1";
    }();
    return enabled;
  }
  base::TimeTicks start_;
  base::TimeTicks last_;
  std::string stages_;
};

constexpr size_t kChunkKeyBytes = 16;
constexpr size_t kChunkRefBytes = 48;
constexpr size_t kMaxChunkRefs = 4096;

using ChunkKey = std::array<uint8_t, kChunkKeyBytes>;
using ChunkHash = std::array<uint8_t, 32>;

void AppendLE(std::vector<uint8_t>* out, uint64_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i)
    out->push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// First 16 bytes of SHA-256 over [client_id u64][type u32][fragment u32]
// (little endian). A repeated id in one artifact appends its occurrence u32.
ChunkKey MakeChunkKey(const blink::PaintChunk::Id& id, uint32_t occurrence) {
  std::vector<uint8_t> input;
  AppendLE(&input, static_cast<uint64_t>(id.client_id), 8);
  AppendLE(&input, static_cast<uint32_t>(id.type), 4);
  AppendLE(&input, static_cast<uint32_t>(id.fragment), 4);
  if (occurrence)
    AppendLE(&input, occurrence, 4);
  const auto digest = crypto::hash::Sha256(input);
  ChunkKey key;
  std::copy_n(digest.begin(), key.size(), key.begin());
  return key;
}

// Serialized incremental chunks, reused while a chunk's display items still
// share their recorded paint buffers. Blink keeps an unchanged display item's
// buffer when it reuses cached paint, and the entry holds references to every
// buffer, so an address match means identical content. Chunks that play back
// child-frame or media markers depend on per-capture state and are never
// cached.
class ChunkSerializationCache {
 public:
  struct Entry {
    std::vector<cc::PaintRecord> items;
    SkRect bounds;
    ChunkHash hash;
    std::vector<uint8_t> bytes;
    std::vector<PaintSerialization::Exported> exports;
    ImageStates image_states;
    uint64_t used = 0;
  };

  static ChunkSerializationCache& Get() {
    static base::NoDestructor<ChunkSerializationCache> cache;
    return *cache;
  }

  // Opt-in (REACH_DRAWING_VERIFY_CHUNK_CACHE=1): also serialize every hit and
  // log a mismatch with the cached bytes.
  static bool Verify() {
    static const bool enabled = [] {
      const char* value = getenv("REACH_DRAWING_VERIFY_CHUNK_CACHE");
      return value && std::string_view(value) == "1";
    }();
    return enabled;
  }

  // The display item records of a chunk, or false when it holds anything else.
  static bool Items(const cc::PaintRecord& record,
                    std::vector<cc::PaintRecord>* items) {
    for (const cc::PaintOp& op : record.buffer()) {
      if (op.GetType() != cc::PaintOpType::kDrawRecord) return false;
      items->push_back(static_cast<const cc::DrawRecordOp&>(op).record);
    }
    return true;
  }

  const Entry* Find(const ChunkKey& key,
                    const std::vector<cc::PaintRecord>& items,
                    const SkRect& bounds, const ImageStates& image_states) {
    auto found = entries_.find(key);
    if (found == entries_.end() || found->second.bounds != bounds ||
        found->second.items.size() != items.size() ||
        found->second.image_states != image_states) {
      return nullptr;
    }
    for (size_t i = 0; i < items.size(); ++i) {
      if (&found->second.items[i].buffer() != &items[i].buffer())
        return nullptr;
    }
    found->second.used = capture_;
    return &found->second;
  }

  void Store(const ChunkKey& key, Entry entry) {
    entry.used = capture_;
    if (auto found = entries_.find(key); found != entries_.end()) {
      bytes_ -= found->second.bytes.size();
      entries_.erase(found);
    }
    if (entry.bytes.size() > kMaxBytes / 4) return;
    bytes_ += entry.bytes.size();
    entries_.emplace(key, std::move(entry));
  }

  // Drops entries no capture has used recently, and everything over budget.
  void EndCapture() {
    ++capture_;
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (capture_ - it->second.used > kRetainedCaptures) {
        bytes_ -= it->second.bytes.size();
        it = entries_.erase(it);
      } else {
        ++it;
      }
    }
    if (bytes_ > kMaxBytes || entries_.size() > kMaxChunkRefs * 2) {
      entries_.clear();
      bytes_ = 0;
    }
  }

 private:
  static constexpr uint64_t kRetainedCaptures = 16;
  static constexpr size_t kMaxBytes = 32 * 1024 * 1024;
  std::map<ChunkKey, Entry> entries_;
  size_t bytes_ = 0;
  uint64_t capture_ = 0;
};

// Repeats a cached chunk's original exports with the same budget checks. Returns
// false, without exporting, when any original would need its bytes sent.
bool ReplayChunkExports(PaintSerialization* state,
                        const ChunkSerializationCache::Entry& entry) {
  for (const auto& exported : entry.exports) {
    const std::string key =
        (exported.kind == sk_reach::kImage ? "image:" : "font:") +
        sk_reach::Hex(exported.hash);
    if (!state->original_ids.contains(key) &&
        !(state->sink && state->known_originals &&
          state->known_originals->contains(key))) {
      return false;
    }
  }
  for (const auto& exported : entry.exports) {
    if (!state->Export(
            exported.kind, exported.hash, exported.size,
            [] { return sk_sp<const SkData>(); }, exported.pixels)) {
      if (state->error.empty())
        state->error = "Cached chunk original export failed";
      return true;
    }
  }
  return true;
}

struct SkeletonSerialization {
  std::map<const SkPicture*, size_t> placeholders;
  const std::vector<ChunkKey>* keys = nullptr;
  std::vector<size_t> referenced;
  std::set<size_t> seen;
};

SkSerialReturnType SerializeChunkReference(SkPicture* picture, void* context) {
  auto* skeleton = static_cast<SkeletonSerialization*>(context);
  const auto found = skeleton->placeholders.find(picture);
  if (found == skeleton->placeholders.end())
    return nullptr;
  if (skeleton->seen.insert(found->second).second)
    skeleton->referenced.push_back(found->second);
  const auto& key = (*skeleton->keys)[found->second];
  return SkData::MakeWithCopy(key.data(), key.size());
}

void AppendF32(std::vector<uint8_t>* out, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendLE(out, bits, 4);
}

// Lets the client scroll the root scroller locally before the next capture
// arrives. The kept chunks are re-emitted in paint order as segments: runs
// under the root scroll translation (and its overflow clip) are converted into
// the scroller's content space, all others into root space. The client
// translates content segments by its predicted offset. Empty when the root
// scroller cannot be expressed this way.
//
// "RSL1", f32 offset/min/max x,y, f32 root-from-content translation x,y,
// f32 root-space scroll clip x,y,w,h, u8 flags (1 = no blocking wheel
// listeners), u16 count + f32 x,y,w,h viewport rects where a wheel may scroll
// something else, u16 count + per segment [u8 kind (0 root, 1 content,
// 2 sticky content + constraint, see below)][u32 length][SKP]. Little-endian.
std::vector<uint8_t> CaptureScrollLayout(
    blink::LocalFrame* frame, const gfx::Rect& bounds, const gfx::Rect& cull,
    const std::map<const blink::PaintChunk*, uint32_t>& chunk_index,
    const cc::PlaybackCallbacks& callbacks, const SkSerialProcs& procs) {
  std::vector<uint8_t> out;
  auto* layout_view = frame->ContentLayoutObject();
  const auto* properties =
      layout_view ? layout_view->FirstFragment().PaintProperties() : nullptr;
  const auto* scroll = properties ? properties->ScrollTranslation() : nullptr;
  const auto* overflow = properties ? properties->OverflowClip() : nullptr;
  auto* viewport = frame->View()->LayoutViewport();
  if (!scroll || !overflow || !viewport)
    return out;
  const auto& root = blink::TransformPaintPropertyNode::Root();
  const auto content =
      blink::GeometryMapper::SourceToDestinationProjection(*scroll, root);
  const auto clip_space = blink::GeometryMapper::SourceToDestinationProjection(
      overflow->LocalTransformSpace(), root);
  if (!content.IsIdentityOr2dTranslation() ||
      !clip_space.IsIdentityOr2dTranslation())
    return out;
  // Segment kinds: 0 root (viewport-fixed), 1 content, 2 content under one
  // position:sticky node whose constraint is relative to the root scroller.
  // Sticky content is drawn by the client with the compositor's sticky offset
  // recomputed at the predicted scroll position, so a stuck header stays put.
  struct Kind {
    uint8_t kind = 0;
    const blink::CompositorStickyConstraint* sticky = nullptr;
    bool operator==(const Kind&) const = default;
  };
  const auto& root_scroll_id = scroll->ScrollNode()
                                   ? scroll->ScrollNode()->GetCompositorElementId()
                                   : blink::CompositorElementId();
  // Anchors (bit 0 left, 1 right, 2 top, 3 bottom) the root scroller moves.
  const auto root_anchors = [&](const blink::CompositorStickyConstraint& c) {
    uint8_t anchors = 0;
    if (root_scroll_id && c.x_scroll_ancestor_element_id == root_scroll_id)
      anchors |= (c.is_anchored_left ? 1 : 0) | (c.is_anchored_right ? 2 : 0);
    if (root_scroll_id && c.y_scroll_ancestor_element_id == root_scroll_id)
      anchors |= (c.is_anchored_top ? 4 : 0) | (c.is_anchored_bottom ? 8 : 0);
    return anchors;
  };
  // nullopt: the chunk cannot be predicted (nested or shifted root stickies).
  const auto classify =
      [&](const blink::PaintChunk& chunk) -> std::optional<Kind> {
    const auto chunk_state = chunk.properties.Unalias();
    if (!scroll->IsAncestorOf(chunk_state.Transform()) ||
        !overflow->IsAncestorOf(chunk_state.Clip()))
      return Kind{};
    for (const blink::EffectPaintPropertyNode* effect = &chunk_state.Effect();
         effect->Parent(); effect = &effect->Parent()->Unalias()) {
      if (!scroll->IsAncestorOf(effect->LocalTransformSpace()))
        return Kind{};
      if (const auto* output = effect->OutputClip();
          output && !overflow->IsAncestorOf(*output))
        return Kind{};
    }
    Kind kind{1, nullptr};
    for (const blink::TransformPaintPropertyNode* node =
             &chunk_state.Transform();
         node && node != scroll; node = node->UnaliasedParent()) {
      const auto* constraint = node->GetStickyConstraint();
      if (!constraint || !root_anchors(*constraint))
        continue;
      if (kind.sticky || constraint->nearest_element_shifting_sticky_box ||
          constraint->nearest_element_shifting_containing_block)
        return std::nullopt;
      kind = Kind{2, constraint};
    }
    return kind;
  };
  const auto& artifact = frame->View()->GetPaintArtifact();
  std::vector<std::pair<Kind, std::vector<const blink::PaintChunk*>>> runs;
  for (const auto& chunk : artifact.GetPaintChunks()) {
    if (!chunk_index.contains(&chunk))
      continue;
    const auto kind = classify(chunk);
    if (!kind)
      return out;
    if (runs.empty() || !(runs.back().first == *kind)) {
      if (runs.size() >= 64)
        return {};
      runs.emplace_back(*kind, std::vector<const blink::PaintChunk*>());
    }
    runs.back().second.push_back(&chunk);
  }
  if (std::none_of(runs.begin(), runs.end(),
                   [](const auto& run) { return run.first.kind != 0; }))
    return out;
  // The compositor's scroll position (cc TransformNode::scroll_offset).
  const auto scroll_position = -scroll->Get2dTranslation();
  const auto translation = content.To2dTranslation();
  gfx::Rect content_cull = cull;
  content_cull.Offset(-std::lround(translation.x()),
                      -std::lround(translation.y()));
  const blink::PropertyTreeState content_state(
      *scroll, *overflow, blink::EffectPaintPropertyNode::Root());
  // [u8 kind] + for sticky: [u8 anchors][f32 left,right,top,bottom offsets]
  // [f32 constraint box, sticky box, containing block rects (x,y,w,h)]
  // [f32 pixel snap x,y][f32 scroll position x,y].
  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> segments;
  for (const auto& [kind, members] : runs) {
    const bool in_content = kind.kind != 0;
    std::vector<uint8_t> header{kind.kind};
    if (const auto* c = kind.sticky) {
      header.push_back(root_anchors(*c));
      for (const float value :
           {c->left_offset, c->right_offset, c->top_offset, c->bottom_offset})
        AppendF32(&header, value);
      for (const auto& rect :
           {c->constraint_box_rect, c->scroll_container_relative_sticky_box_rect,
            c->scroll_container_relative_containing_block_rect})
        for (const float value : {rect.x(), rect.y(), rect.width(), rect.height()})
          AppendF32(&header, value);
      for (const float value :
           {c->pixel_snap_offset.x(), c->pixel_snap_offset.y(),
            scroll_position.x(), scroll_position.y()})
        AppendF32(&header, value);
    }
    blink::PaintChunkSubset subset(artifact, *members.front());
    for (size_t i = 1; i < members.size(); ++i)
      subset.Merge(blink::PaintChunkSubset(artifact, *members[i]));
    bool missing = false;
    auto record = blink::PaintChunksToCcLayer::Convert(
        subset,
        in_content ? content_state : blink::PropertyTreeState::Root(),
        in_content ? &content_cull : &cull,
        [&](const blink::PaintChunk& chunk, cc::PaintRecord) {
          const auto found = chunk_index.find(&chunk);
          if (found == chunk_index.end()) {
            missing = true;
            return cc::PaintRecord();
          }
          cc::PaintOpBuffer marker;
          marker.push<cc::CustomDataOp>(found->second);
          return marker.ReleaseAsRecord();
        });
    PaintSerialization check;
    check.collect_paint_ids = true;
    if (missing || !CheckPaintRecord(&check, record, 0))
      return {};
    const auto picture_bounds =
        in_content ? gfx::RectToSkRect(content_cull)
                   : SkRect::MakeWH(bounds.width(), bounds.height());
    auto picture = record.ToSkPicture(picture_bounds, nullptr, callbacks);
    if (!picture)
      return {};
    BoundedPaintStream stream(kMaxPictureBytes);
    picture->serialize(&stream, &procs);
    if (stream.failed() || !stream.bytesWritten())
      return {};
    segments.emplace_back(std::move(header), stream.TakeBytes());
  }
  out.insert(out.end(), {'R', 'S', 'L', '1'});
  const auto offset = viewport->GetScrollOffset();
  const auto minimum = viewport->MinimumScrollOffset();
  const auto maximum = viewport->MaximumScrollOffset();
  const bool horizontal =
      viewport->UserInputScrollable(blink::kHorizontalScrollbar);
  const bool vertical = viewport->UserInputScrollable(blink::kVerticalScrollbar);
  for (const float value :
       {offset.x(), offset.y(), horizontal ? minimum.x() : offset.x(),
        vertical ? minimum.y() : offset.y(),
        horizontal ? maximum.x() : offset.x(),
        vertical ? maximum.y() : offset.y(), translation.x(), translation.y()})
    AppendF32(&out, value);
  auto clip = overflow->PaintClipRect().Rect();
  clip.Offset(clip_space.To2dTranslation());
  for (const float value : {clip.x(), clip.y(), clip.width(), clip.height()})
    AppendF32(&out, value);
  std::vector<gfx::Rect> others;
  for (const auto& entry : frame->View()->ScrollableAreas()) {
    const auto* area = entry.value.Get();
    if (static_cast<const blink::ScrollableArea*>(area) == viewport ||
        !area->ScrollsOverflow() || !area->GetLayoutBox())
      continue;
    others.push_back(area->GetLayoutBox()->AbsoluteBoundingBoxRect());
  }
  for (blink::Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (const auto* owner = child->OwnerLayoutObject())
      others.push_back(owner->AbsoluteBoundingBoxRect());
  }
  std::erase_if(others,
                [&](const gfx::Rect& rect) { return !rect.Intersects(bounds); });
  const bool predictable =
      others.size() <= 64 &&
      !frame->GetEventHandlerRegistry().HasEventHandlers(
          blink::EventHandlerRegistry::kWheelEventBlocking);
  out.push_back(predictable ? 1 : 0);
  if (!predictable)
    others.clear();
  AppendLE(&out, others.size(), 2);
  for (const auto& rect : others)
    for (const int value : {rect.x(), rect.y(), rect.width(), rect.height()})
      AppendF32(&out, value);
  AppendLE(&out, segments.size(), 2);
  for (const auto& [header, bytes] : segments) {
    out.insert(out.end(), header.begin(), header.end());
    AppendLE(&out, bytes.size(), 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
  }
  if (out.size() > kMaxPictureBytes)
    return {};
  return out;
}

// Plays back a skeleton and reports, for every chunk placeholder drawn, the
// viewport pixels its drawing can affect and a hash of the transform, clip and
// enclosing layer effects applied to it. Clips are tracked conservatively
// (bounds only), and layer effects that may touch pixels outside the drawn
// content expand the rect to the clip at the layer's save.
class DamageCanvas final : public SkNoDrawCanvas {
 public:
  struct Entry {
    size_t index;
    SkIRect rect;
    uint32_t state;
  };

  DamageCanvas(int width, int height,
               const std::map<const SkPicture*, size_t>* placeholders)
      : SkNoDrawCanvas(width, height),
        viewport_(SkIRect::MakeWH(width, height)),
        placeholders_(placeholders) {}

  const std::vector<Entry>& entries() const { return entries_; }
  bool overflowed() const { return overflowed_; }

 protected:
  void willSave() override { frames_.emplace_back(); }

  bool onDoSaveBehind(const SkRect*) override {
    frames_.emplace_back();
    return false;
  }

  SaveLayerStrategy getSaveLayerStrategy(const SaveLayerRec& rec) override {
    Frame frame;
    frame.layer = true;
    frame.ctm = getTotalMatrix();
    frame.clip = getDeviceClipBounds();
    frame.backdrop = rec.fBackdrop;
    if (rec.fPaint) {
      frame.alpha = rec.fPaint->getAlphaf();
      const auto mode = rec.fPaint->asBlendMode();
      frame.blend = mode ? static_cast<int>(*mode) : -1;
      frame.filter = rec.fPaint->getImageFilter();
      frame.color_filter = rec.fPaint->getColorFilter();
    }
    // Several filters, or effects applied over the prior contents, are not
    // bounded by the drawn content.
    frame.fills_clip =
        frame.backdrop || frame.color_filter || !rec.fFilters.empty() ||
        ((rec.fSaveLayerFlags & kInitWithPrevious_SaveLayerFlag) &&
         (frame.filter || frame.alpha < 1.f || frame.blend != kSrcOverMode));
    switch (frame.blend) {
      case -1:
      case static_cast<int>(SkBlendMode::kClear):
      case static_cast<int>(SkBlendMode::kSrc):
      case static_cast<int>(SkBlendMode::kSrcIn):
      case static_cast<int>(SkBlendMode::kDstIn):
      case static_cast<int>(SkBlendMode::kSrcOut):
      case static_cast<int>(SkBlendMode::kDstATop):
      case static_cast<int>(SkBlendMode::kModulate):
        // Transparent layer pixels change the destination.
        frame.fills_clip = true;
        break;
      default:
        break;
    }
    frame.effects = FlattenableHash(frame.filter) ^
                    (FlattenableHash(frame.backdrop) * 31u) ^
                    (FlattenableHash(frame.color_filter) * 961u);
    for (const auto& filter : rec.fFilters)
      frame.effects = frame.effects * 31u + FlattenableHash(filter.get());
    frames_.push_back(frame);
    return kNoLayer_SaveLayerStrategy;
  }

  void willRestore() override {
    if (!frames_.empty())
      frames_.pop_back();
  }

  void onDrawPicture(const SkPicture* picture, const SkMatrix* matrix,
                     const SkPaint* paint) override {
    const auto found = placeholders_->find(picture);
    if (found == placeholders_->end()) {
      SkCanvas::onDrawPicture(picture, matrix, paint);
      return;
    }
    if (!matrix && !paint) {
      Record(found->second, picture->cullRect());
      return;
    }
    // Same state as SkCanvas::drawPicture applies before playback.
    const int count = getSaveCount();
    if (paint) {
      SkRect layer_bounds = picture->cullRect();
      if (matrix)
        layer_bounds = matrix->mapRect(layer_bounds);
      saveLayer(&layer_bounds, paint);
    } else {
      save();
    }
    if (matrix)
      concat(*matrix);
    Record(found->second, picture->cullRect());
    restoreToCount(count);
  }

 private:
  static constexpr int kSrcOverMode = static_cast<int>(SkBlendMode::kSrcOver);
  static constexpr size_t kMaxEntries = 65536;

  struct Frame {
    bool layer = false;
    SkMatrix ctm;
    SkIRect clip = SkIRect::MakeEmpty();
    float alpha = 1.f;
    int blend = kSrcOverMode;
    const SkImageFilter* filter = nullptr;
    const SkImageFilter* backdrop = nullptr;
    const SkColorFilter* color_filter = nullptr;
    bool fills_clip = false;
    uint32_t effects = 0;
  };

  // Hash of an effect's serialized form; nested images, pictures and
  // typefaces contribute their identity only.
  uint32_t FlattenableHash(const SkFlattenable* flattenable) {
    if (!flattenable)
      return 0;
    const auto [cached, inserted] = flattenables_.try_emplace(flattenable, 1u);
    if (inserted) {
      SkSerialProcs procs;
      procs.fImageProc = [](SkImage* image, void*) -> SkSerialReturnType {
        const uint32_t id = image->uniqueID();
        return SkData::MakeWithCopy(&id, sizeof(id));
      };
      procs.fPictureProc = [](SkPicture* nested, void*) -> SkSerialReturnType {
        const uint32_t id = nested->uniqueID();
        return SkData::MakeWithCopy(&id, sizeof(id));
      };
      procs.fTypefaceProc = [](SkTypeface* typeface,
                               void*) -> SkSerialReturnType {
        const uint32_t id = typeface->uniqueID();
        return SkData::MakeWithCopy(&id, sizeof(id));
      };
      if (const auto data = flattenable->serialize(&procs)) {
        cached->second = static_cast<uint32_t>(base::FastHash(std::string_view(
            static_cast<const char*>(data->data()), data->size())));
      }
    }
    return cached->second;
  }

  template <typename T>
  void Mix(const T& value) {
    scratch_.append(reinterpret_cast<const char*>(&value), sizeof(value));
  }

  void MixRect(const SkIRect& rect) {
    for (const int32_t value : {rect.fLeft, rect.fTop, rect.fRight,
                                rect.fBottom}) {
      Mix(value);
    }
  }

  void Record(size_t index, const SkRect& cull) {
    if (entries_.size() >= kMaxEntries) {
      overflowed_ = true;
      return;
    }
    const SkMatrix total = getTotalMatrix();
    const SkIRect clip = getDeviceClipBounds();
    scratch_.clear();
    std::array<SkScalar, 9> values;
    total.get9(values.data());
    Mix(values);
    MixRect(clip);

    SkRect rect = SkRect::Make(clip);
    if (!total.hasPerspective()) {
      const SkRect mapped = total.mapRect(cull);
      if (mapped.isFinite() && !rect.intersect(mapped))
        rect.setEmpty();
    }
    // Innermost layer first: a filter spreads its (clipped) input, and its
    // output is clipped by the clip in effect when the layer was saved.
    for (auto frame = frames_.rbegin(); frame != frames_.rend(); ++frame) {
      if (!frame->layer)
        continue;
      const SkRect layer_clip = SkRect::Make(frame->clip);
      if (frame->fills_clip) {
        rect = layer_clip;
      } else if (frame->filter) {
        SkMatrix inverse;
        if (!frame->filter->canComputeFastBounds() ||
            frame->ctm.hasPerspective() || !frame->ctm.invert(&inverse)) {
          rect = layer_clip;
        } else if (!rect.isEmpty()) {
          SkRect spread = frame->ctm.mapRect(
              frame->filter->computeFastBounds(inverse.mapRect(rect)));
          if (!spread.isFinite())
            spread = layer_clip;
          if (!spread.intersect(layer_clip))
            spread.setEmpty();
          rect = spread;
        }
      }
    }
    for (const auto& frame : frames_) {
      if (!frame.layer)
        continue;
      Mix(frame.alpha);
      Mix(frame.blend);
      Mix(frame.effects);
      MixRect(frame.clip);
      std::array<SkScalar, 9> layer_values;
      frame.ctm.get9(layer_values.data());
      Mix(layer_values);
    }

    SkIRect device = SkIRect::MakeEmpty();
    if (!rect.isEmpty() && rect.isFinite()) {
      device = rect.roundOut();
      device.outset(1, 1);
      if (!device.intersect(viewport_))
        device.setEmpty();
    }
    if (device.isEmpty())
      device.setEmpty();
    entries_.push_back(
        {index, device, static_cast<uint32_t>(base::FastHash(scratch_))});
  }

  const SkIRect viewport_;
  const std::map<const SkPicture*, size_t>* const placeholders_;
  std::vector<Frame> frames_;
  std::vector<Entry> entries_;
  std::map<const SkFlattenable*, uint32_t> flattenables_;
  std::string scratch_;
  bool overflowed_ = false;
};

void AppendHex(std::string* out, base::span<const uint8_t> bytes) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  for (const uint8_t byte : bytes) {
    out->push_back(kDigits[byte >> 4]);
    out->push_back(kDigits[byte & 15]);
  }
}

// Damage annotations: compact JSON, split into parts of at most 4000 bytes,
// {"damage":1,"part":P,"parts":N,"w":W,"h":H,"e":[[key,x,y,r,b,state],...]}
// with entries in placeholder paint order. Empty when they do not fit in
// `max_parts` annotations.
std::vector<std::string> FormatDamage(const DamageCanvas& canvas,
                                      const std::vector<ChunkKey>& keys,
                                      int width, int height,
                                      size_t max_parts) {
  constexpr size_t kMaxAnnotationBytes = 4000;
  // Upper bound of the per-part JSON envelope.
  constexpr size_t kEnvelopeBytes = 96;
  if (canvas.overflowed() || !max_parts)
    return {};
  std::vector<std::string> bodies(1);
  std::string entry;
  for (const auto& damage : canvas.entries()) {
    if (damage.index >= keys.size())
      return {};
    entry = "[\"";
    AppendHex(&entry, keys[damage.index]);
    entry += "\",";
    for (const int32_t value : {damage.rect.fLeft, damage.rect.fTop,
                                damage.rect.fRight, damage.rect.fBottom}) {
      entry += base::NumberToString(value);
      entry += ',';
    }
    entry += '"';
    const uint32_t state = damage.state;
    const std::array<uint8_t, 4> state_bytes = {
        static_cast<uint8_t>(state >> 24), static_cast<uint8_t>(state >> 16),
        static_cast<uint8_t>(state >> 8), static_cast<uint8_t>(state)};
    AppendHex(&entry, state_bytes);
    entry += "\"]";
    if (!bodies.back().empty() &&
        bodies.back().size() + 1 + entry.size() >
            kMaxAnnotationBytes - kEnvelopeBytes) {
      if (bodies.size() >= max_parts)
        return {};
      bodies.emplace_back();
    }
    if (!bodies.back().empty())
      bodies.back() += ',';
    bodies.back() += entry;
  }
  std::vector<std::string> parts;
  for (size_t part = 0; part < bodies.size(); ++part) {
    std::string text = "{\"damage\":1,\"part\":" + base::NumberToString(part) +
                       ",\"parts\":" + base::NumberToString(bodies.size()) +
                       ",\"w\":" + base::NumberToString(width) +
                       ",\"h\":" + base::NumberToString(height) +
                       ",\"e\":[" + bodies[part] + "]}";
    if (text.size() > kMaxAnnotationBytes)
      return {};
    parts.push_back(std::move(text));
  }
  return parts;
}

sk_sp<SkPicture> FinishViewportPicture(SkPictureRecorder* recorder,
                                     const SkRect& viewport) {
  auto picture = recorder->finishRecordingAsPicture();
  // Empty recordings and serialized placeholders lose their cull bounds on
  // deserialization. A nonpainting annotation survives the round trip.
  if (picture->cullRect().isEmpty() && picture->approximateOpCount() == 0) {
    auto* canvas = recorder->beginRecording(viewport);
    canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.EmptyViewport.v1", nullptr);
    return recorder->finishRecordingAsPicture();
  }
  return picture;
}

// The skeleton is the normal lifecycle conversion of the local root's paint
// artifact, with each kept chunk's drawings replaced by a placeholder picture
// that serializes as its 16-byte key. Chunks are culled as a whole against the
// viewport expanded by one viewport height above and below.
CefDrawingCommands CaptureIncrementalPicture(
    blink::LocalFrame* frame, const gfx::Rect& bounds,
    const std::map<uint32_t, sk_sp<SkPicture>>& frame_pictures,
    const std::map<uint32_t, sk_sp<SkData>>& media_states,
    const std::vector<uint8_t>& known_bytes,
    const std::function<void(const std::string&, const void*, size_t)>& sink,
    const std::set<std::string>* known_originals) {
  CefDrawingCommands result;
  if (known_bytes.size() % kChunkRefBytes ||
      known_bytes.size() / kChunkRefBytes > kMaxChunkRefs) {
    result.error = "Invalid known paint chunk set";
    return result;
  }
  std::map<ChunkKey, ChunkHash> known;
  for (size_t offset = 0; offset < known_bytes.size();
       offset += kChunkRefBytes) {
    ChunkKey key;
    ChunkHash hash;
    std::copy_n(known_bytes.begin() + offset, key.size(), key.begin());
    std::copy_n(known_bytes.begin() + offset + key.size(), hash.size(),
                hash.begin());
    if (!known.emplace(key, hash).second) {
      result.error = "Duplicate known paint chunk key";
      return result;
    }
  }
  if (!frame->IsAttached() || !frame->View() || !frame->GetDocument() ||
      frame->GetDocument()->Lifecycle().GetState() !=
          blink::DocumentLifecycle::kPaintClean) {
    result.error = "Frame is not paint-clean for incremental capture";
    return result;
  }
  struct Chunk {
    ChunkKey key;
    cc::PaintRecord record;
    SkRect bounds;
  };
  std::vector<Chunk> chunks;
  std::map<const blink::PaintChunk*, uint32_t> chunk_index;
  std::map<std::tuple<uint64_t, uint32_t, uint32_t>, uint32_t> occurrences;
  bool overflow = false;
  const auto cull = CommandPaintCull(bounds);
  auto skeleton_record = ConvertCommandPaint(
      frame, cull,
      [&](const blink::PaintChunk& chunk, cc::PaintRecord items) {
        if (chunks.size() >= kMaxChunkRefs) {
          overflow = true;
          return cc::PaintRecord();
        }
        const auto occurrence = occurrences[{
            static_cast<uint64_t>(chunk.id.client_id),
            static_cast<uint32_t>(chunk.id.type), chunk.id.fragment}]++;
        cc::PaintOpBuffer marker;
        marker.push<cc::CustomDataOp>(static_cast<uint32_t>(chunks.size()));
        chunk_index.emplace(&chunk, static_cast<uint32_t>(chunks.size()));
        chunks.push_back({MakeChunkKey(chunk.id, occurrence), std::move(items),
                          gfx::RectToSkRect(chunk.drawable_bounds)});
        return marker.ReleaseAsRecord();
      });
  CaptureTimer::Get().Mark("convert");
  if (overflow) {
    result.error = "Paint chunk reference budget exceeded";
    return result;
  }
  PaintSerialization state;
  state.sink = sink;
  state.known_originals = known_originals;
  // The skeleton holds only property-state operations and chunk markers.
  state.collect_paint_ids = true;
  if (!CheckPaintRecord(&state, skeleton_record, 0)) {
    result.error = state.error;
    return result;
  }
  const auto skeleton_image_states = std::move(state.image_states);
  for (const auto id : state.media_ids) {
    if (id >= chunks.size()) {
      result.error = "Unknown paint chunk marker";
      return result;
    }
  }
  state.collect_paint_ids = false;
  state.media_ids.clear();
  for (const auto& [id, picture] : frame_pictures)
    state.media_ids.insert(id);
  for (const auto& [id, data] : media_states)
    state.media_ids.insert(id);
  std::vector<ImageStates> chunk_image_states;
  chunk_image_states.reserve(chunks.size());
  for (const auto& chunk : chunks) {
    state.image_states.clear();
    if (!CheckPaintRecord(&state, chunk.record, 0)) {
      result.error = state.error;
      return result;
    }
    chunk_image_states.push_back(std::move(state.image_states));
  }
  std::vector<ChunkKey> keys;
  std::vector<sk_sp<SkPicture>> placeholders;
  SkeletonSerialization skeleton;
  skeleton.keys = &keys;
  for (const auto& chunk : chunks) {
    keys.push_back(chunk.key);
    placeholders.push_back(SkPicture::MakePlaceholder(chunk.bounds));
    skeleton.placeholders.emplace(placeholders.back().get(),
                                  placeholders.size() - 1);
  }
  cc::PlaybackCallbacks skeleton_callbacks;
  skeleton_callbacks.custom_callback = base::BindRepeating(
      [](const std::vector<sk_sp<SkPicture>>* placeholders,
         PaintSerialization* state, SkCanvas* canvas, uint32_t id) {
        if (id >= placeholders->size()) {
          state->error = "Unknown paint chunk marker";
          return;
        }
        canvas->drawPicture((*placeholders)[id]);
      }, base::Unretained(&placeholders), base::Unretained(&state));
  auto picture = skeleton_record.ToSkPicture(
      SkRect::MakeWH(bounds.width(), bounds.height()), nullptr,
      skeleton_callbacks);
  if (!state.error.empty() || !picture) {
    result.error =
        state.error.empty() ? "Skia skeleton recording failed" : state.error;
    return result;
  }
  // Where each kept chunk lands in the viewport, so the client can limit
  // rasterization to changed regions. Optional: clients redraw everything
  // when a skeleton carries no damage annotations.
  std::vector<std::string> damage;
  {
    constexpr size_t kMaxAnnotations = 1024;
    constexpr size_t kMaxDamageParts = 1000;
    DamageCanvas damage_canvas(bounds.width(), bounds.height(),
                               &skeleton.placeholders);
    picture->playback(&damage_canvas);
    if (media_states.size() < kMaxAnnotations) {
      damage = FormatDamage(
          damage_canvas, keys, bounds.width(), bounds.height(),
          std::min(kMaxDamageParts, kMaxAnnotations - media_states.size()));
    }
  }
  CaptureTimer::Get().Mark("damage");
  SkSerialProcs procs;
  procs.fImageProc = &SerializeOriginalImage;
  procs.fImageCtx = &state;
  procs.fTypefaceProc = &SerializeOriginalTypeface;
  procs.fTypefaceCtx = &state;
  procs.fPictureProc = &SerializeChunkReference;
  procs.fPictureCtx = &skeleton;
  const auto serialize_skeleton =
      [&](bool with_damage) -> std::optional<std::vector<uint8_t>> {
    skeleton.referenced.clear();
    skeleton.seen.clear();
    SkPictureRecorder media_recorder;
    const auto viewport = SkRect::MakeWH(bounds.width(), bounds.height());
    auto* media_canvas = media_recorder.beginRecording(viewport);
    if (!DrawImageStates(media_canvas, skeleton_image_states, &state.error))
      return std::nullopt;
    for (const auto& [id, data] : media_states) {
      media_canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.MediaState.v1",
                                   data.get());
    }
    if (with_damage) {
      for (const auto& text : damage) {
        const auto data = SkData::MakeWithCopy(text.data(), text.size());
        media_canvas->drawAnnotation(SkRect::MakeEmpty(),
                                     "Reach.MediaState.v1", data.get());
      }
    }
    // Replaying into a recorder keeps placeholder references intact.
    picture->playback(media_canvas);
    BoundedPaintStream stream(kMaxPictureBytes);
    FinishViewportPicture(&media_recorder, viewport)->serialize(&stream, &procs);
    if (stream.failed())
      return std::nullopt;
    return stream.TakeBytes();
  };
  auto skeleton_bytes = serialize_skeleton(!damage.empty());
  if (!skeleton_bytes && state.error.empty() && !damage.empty()) {
    damage.clear();
    skeleton_bytes = serialize_skeleton(false);
  }
  CaptureTimer::Get().Mark("skeleton");
  if (!state.error.empty() || !skeleton_bytes) {
    result.error = state.error.empty() ? "Serialized skeleton budget exceeded"
                                       : state.error;
    return result;
  }
  result.picture = std::move(*skeleton_bytes);
  if (result.picture.empty()) {
    result.error = "Skia produced an empty skeleton";
    return result;
  }
  result.scroll = CaptureScrollLayout(frame, bounds, cull, chunk_index,
                                      skeleton_callbacks, procs);
  CaptureTimer::Get().Mark("scroll");
  // Chunk pictures compose child frames and media markers like the full
  // picture, including complete child/media content in prefetched chunks.
  cc::PlaybackCallbacks chunk_callbacks;
  chunk_callbacks.custom_callback = base::BindRepeating(
      [](const std::map<uint32_t, sk_sp<SkData>>* states,
         const std::map<uint32_t, sk_sp<SkPicture>>* frames,
         PaintSerialization* state, SkCanvas* canvas, uint32_t id) {
        ++state->markers;
        const auto child = frames->find(id);
        if (child != frames->end()) {
          SkAutoCanvasRestore restore(canvas, true);
          const auto bounds = child->second->cullRect();
          canvas->scale(1 / bounds.width(), 1 / bounds.height());
          child->second->playback(canvas);
          return;
        }
        if (!states->contains(id)) {
          state->error = "Unknown media paint identity";
          return;
        }
        const auto text = base::NumberToString(id);
        auto data = SkData::MakeWithCopy(text.data(), text.size());
        canvas->drawAnnotation(SkRect::MakeWH(1, 1), "Reach.Video.v1",
                               data.get());
      }, base::Unretained(&media_states), base::Unretained(&frame_pictures),
      base::Unretained(&state));
  SkSerialProcs chunk_procs;
  chunk_procs.fImageProc = &SerializeOriginalImage;
  chunk_procs.fImageCtx = &state;
  chunk_procs.fTypefaceProc = &SerializeOriginalTypeface;
  chunk_procs.fTypefaceCtx = &state;
  size_t new_bytes = 0;
  size_t all_bytes = 0;
  size_t cached = 0;
  auto& cache = ChunkSerializationCache::Get();
  base::TimeDelta recording, serializing, hashing;
  for (const auto index : skeleton.referenced) {
    const auto& chunk = chunks[index];
    std::vector<cc::PaintRecord> items;
    const bool cacheable = ChunkSerializationCache::Items(chunk.record, &items);
    const ChunkSerializationCache::Entry* hit =
        cacheable ? cache.Find(chunk.key, items, chunk.bounds,
                               chunk_image_states[index]) : nullptr;
    if (hit && ReplayChunkExports(&state, *hit)) {
      if (!state.error.empty()) {
        result.error = state.error;
        return result;
      }
      if (ChunkSerializationCache::Verify()) {
        PaintSerialization scratch;
        SkSerialProcs scratch_procs = chunk_procs;
        scratch_procs.fImageCtx = &scratch;
        scratch_procs.fTypefaceCtx = &scratch;
        BoundedPaintStream stream(kMaxPictureBytes);
        if (auto verified = chunk.record.ToSkPicture(chunk.bounds, nullptr,
                                                     chunk_callbacks)) {
          verified = WithImageStates(std::move(verified),
                                      chunk_image_states[index],
                                      &scratch.error);
          if (!verified) {
            state.error = scratch.error;
            result.error = state.error;
            return result;
          }
          verified->serialize(&stream, &scratch_procs);
        }
        if (stream.failed() ||
            crypto::hash::Sha256(stream.TakeBytes()) != hit->hash) {
          fputs("reach drawing chunk cache mismatch\n", stderr);
        }
      }
      ++cached;
      all_bytes += hit->bytes.size();
      result.refs.insert(result.refs.end(), chunk.key.begin(), chunk.key.end());
      result.refs.insert(result.refs.end(), hit->hash.begin(), hit->hash.end());
      const auto found = known.find(chunk.key);
      if (found != known.end() && found->second == hit->hash)
        continue;
      new_bytes += hit->bytes.size();
      if (new_bytes > kMaxPaintBytes) {
        result.error = "Aggregate new paint chunk budget exceeded";
        return result;
      }
      result.chunks.insert(result.chunks.end(), chunk.key.begin(),
                           chunk.key.end());
      AppendLE(&result.chunks, hit->bytes.size(), 4);
      result.chunks.insert(result.chunks.end(), hit->bytes.begin(),
                           hit->bytes.end());
      continue;
    }
    std::vector<PaintSerialization::Exported> exports;
    state.exports = &exports;
    const size_t markers = state.markers;
    auto mark = base::TimeTicks::Now();
    auto chunk_picture =
        chunk.record.ToSkPicture(chunk.bounds, nullptr, chunk_callbacks);
    chunk_picture = WithImageStates(std::move(chunk_picture),
                                    chunk_image_states[index], &state.error);
    recording += base::TimeTicks::Now() - mark;
    if (!state.error.empty() || !chunk_picture) {
      result.error =
          state.error.empty() ? "Skia chunk recording failed" : state.error;
      return result;
    }
    BoundedPaintStream stream(kMaxPictureBytes);
    mark = base::TimeTicks::Now();
    chunk_picture->serialize(&stream, &chunk_procs);
    serializing += base::TimeTicks::Now() - mark;
    state.exports = nullptr;
    if (!state.error.empty() || stream.failed() || !stream.bytesWritten()) {
      result.error = state.error.empty() ? "Serialized chunk budget exceeded"
                                         : state.error;
      return result;
    }
    auto bytes = stream.TakeBytes();
    all_bytes += bytes.size();
    mark = base::TimeTicks::Now();
    const auto hash = crypto::hash::Sha256(bytes);
    hashing += base::TimeTicks::Now() - mark;
    result.refs.insert(result.refs.end(), chunk.key.begin(), chunk.key.end());
    result.refs.insert(result.refs.end(), hash.begin(), hash.end());
    const auto found = known.find(chunk.key);
    const bool held = found != known.end() && found->second == hash;
    if (!held) {
      new_bytes += bytes.size();
      if (new_bytes > kMaxPaintBytes) {
        result.error = "Aggregate new paint chunk budget exceeded";
        return result;
      }
      result.chunks.insert(result.chunks.end(), chunk.key.begin(),
                           chunk.key.end());
      AppendLE(&result.chunks, bytes.size(), 4);
      result.chunks.insert(result.chunks.end(), bytes.begin(), bytes.end());
    }
    if (cacheable && state.markers == markers) {
      cache.Store(chunk.key, {std::move(items), chunk.bounds, hash,
                              std::move(bytes), std::move(exports),
                              chunk_image_states[index]});
    }
  }
  cache.EndCapture();
  CaptureTimer::Get().Mark("chunks");
  CaptureTimer::Get().Finish("chunks=" + base::NumberToString(chunks.size()) +
                             " referenced=" + base::NumberToString(skeleton.referenced.size()) +
                             " cached=" + base::NumberToString(cached) +
                             " new_bytes=" + base::NumberToString(new_bytes) +
                             " all_bytes=" + base::NumberToString(all_bytes) +
                             " record=" + base::NumberToString(recording.InMilliseconds()) +
                             " serialize=" + base::NumberToString(serializing.InMilliseconds()) +
                             " hash=" + base::NumberToString(hashing.InMilliseconds()));
  const size_t paint_bytes =
      new_bytes + state.bytes + state.font_bytes + state.identity_bytes;
  if (result.picture.size() + paint_bytes > kMaxPaintBytes &&
      !damage.empty()) {
    if (auto plain = serialize_skeleton(false); plain && state.error.empty())
      result.picture = std::move(*plain);
  }
  if (result.picture.size() + paint_bytes > kMaxPaintBytes) {
    result.error = "Aggregate picture and original resource budget exceeded";
    return result;
  }
  result.resources = std::move(state.originals);
  result.width = bounds.width();
  result.height = bounds.height();
  return result;
}

CefDrawingCommands CaptureDrawingCommandsImpl(
    blink::WebLocalFrame* web_frame, const std::string& expected_document,
    const std::map<std::string, std::vector<uint8_t>>& children,
    const std::map<std::string, std::vector<uint8_t>>& resources,
    const std::function<void(const std::string&, const void*, size_t)>& sink,
    const std::vector<uint8_t>* known,
    const std::set<std::string>* known_originals = nullptr) {
  CefDrawingCommands result;
  blink::Persistent<blink::LocalFrame> frame =
      blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetPage() ||
      !frame->GetDocument() || !frame->View()) {
    result.error = "Frame is detached or not ready";
    return result;
  }
  blink::Persistent<blink::Document> document = frame->GetDocument();
  if (!expected_document.empty() &&
      document->Token().ToString() != expected_document) {
    result.error = "Frame document changed during command composition";
    return result;
  }
  blink::Persistent<blink::Page> page = frame->GetPage();
  // Visual update requests made from here on (including by this capture's own
  // animation and lifecycle steps) are reported again for the next capture.
  RequestedFrameRoots().erase(&frame->LocalFrameRoot());
  PendingAnimation::From(*document).pending = false;
  CaptureTimer::Get().Start();
  // Held begin frames also suppress the compositor's idle-period signal.
  // Let Blink schedule idle work normally between command captures.
  static_cast<blink::scheduler::MainThreadSchedulerImpl&>(
      blink::scheduler::WebThreadScheduler::MainThreadScheduler())
      .BeginFrameNotExpectedSoon();
  page->Animator().ServiceScriptedAnimations(base::TimeTicks::Now());
  CaptureTimer::Get().Mark("raf");
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during animation";
    return result;
  }
  page->Animator().UpdateAllLifecyclePhasesExceptPaint(
      *frame, blink::DocumentUpdateReason::kInspector);
  CaptureTimer::Get().Mark("layout");
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during layout";
    return result;
  }
  // Start pending CSS animations and transitions on the main thread;
  // no compositor commit exists to start them there.
  for (blink::Frame* tree_frame = frame.Get(); tree_frame;
       tree_frame = tree_frame->Tree().TraverseNext(frame.Get())) {
    auto* local = blink::DynamicTo<blink::LocalFrame>(tree_frame);
    blink::Document* local_document = local ? local->GetDocument() : nullptr;
    if (!local_document || local_document->Lifecycle().GetState() <
                               blink::DocumentLifecycle::kLayoutClean) {
      continue;
    }
    blink::ScriptForbiddenScope forbid_script;
    local_document->GetPendingAnimations().Update(
        nullptr, /*start_on_compositor=*/false);
  }
  // External begin frames stay held so nothing commits or rasters, which also
  // means BeginMainFrame never runs. Perform its main-thread steps here: the
  // paint-clean lifecycle delivers ResizeObservers and the post-lifecycle
  // steps compute IntersectionObservers (including loading=lazy).
  CaptureTimer::Get().Mark("animations");
  page->Animator().UpdateAllLifecyclePhases(
      *frame, blink::DocumentUpdateReason::kInspector);
  CaptureTimer::Get().Mark("paint");
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during animation start";
    return result;
  }
  frame->LocalFrameRoot().View()->RunPostLifecycleSteps();
  CaptureTimer::Get().Mark("post");
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during post-lifecycle steps";
    return result;
  }
  auto* root = document->GetLayoutView();
  if (!root) {
    result.error = "Document has no layout";
    return result;
  }
  const auto bounds = frame->View()->LayoutViewport()->VisibleContentRect(
      blink::kIncludeScrollbars);
  PaintSerialization dependencies;
  auto required_recording =
      CaptureRequiredPaint(web_frame, bounds, &dependencies);
  if (!required_recording) {
    result.error = dependencies.error;
    return result;
  }
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get()) {
    result.error = "Frame changed during paint recording";
    return result;
  }
  auto recording = std::move(*required_recording);
  size_t child_bytes = 0;
  OriginalTypefaceCache frame_fonts;
  frame_fonts.originals = &resources;
  std::map<uint32_t, sk_sp<SkPicture>> frame_pictures;
  for (blink::Frame* tree_frame = frame.Get(); tree_frame;
       tree_frame = tree_frame->Tree().TraverseNext(frame.Get())) {
    if (!IsInlinePaintFrame(frame.Get(), tree_frame)) {
      continue;
    }
    auto* tree_document =
        blink::To<blink::LocalFrame>(tree_frame)->GetDocument();
    root = tree_document ? tree_document->GetLayoutView() : nullptr;
    for (blink::LayoutObject* object = root; object;
         object = object->NextInPreOrder(root)) {
      auto* node = object->GetNode();
      if (object->IsLayoutEmbeddedContent()) {
        auto* owner = blink::DynamicTo<blink::HTMLFrameOwnerElement>(node);
        if (!owner || !dependencies.media_ids.contains(owner->GetDomNodeId()))
          continue;
        auto* child = owner ? owner->ContentFrame() : nullptr;
        if (!child || DrawingFrameIdentity(child).empty() ||
            !blink::Canvas2DBitmapProvider::IsReachCommandMode()) {
          result.error = "Embedded plugin or unready frame requires support";
          return result;
        }
        const auto found = children.find(DrawingFrameIdentity(child));
        if (found == children.end() ||
            (child_bytes += found->second.size()) > kMaxPaintBytes) {
          result.error =
              "Missing child frame picture or composition byte budget exceeded";
          return result;
        }
        SkDeserialProcs procs;
        procs.fTypefaceStreamProc = DeserializeOriginalTypeface;
        procs.fTypefaceCtx = &frame_fonts;
        procs.fImageDataProc = DeserializeOriginalImage;
        procs.fImageCtx = &frame_fonts;
        auto picture = SkPicture::MakeFromData(found->second.data(),
                                               found->second.size(), &procs);
        if (frame_fonts.failed || !picture || picture->cullRect().isEmpty() ||
            picture->cullRect().left() != 0 || picture->cullRect().top() != 0) {
          result.error = "Invalid child frame command picture";
          return result;
        }
        frame_pictures.emplace(owner->GetDomNodeId(), std::move(picture));
      }
    }
  }
  std::map<uint32_t, sk_sp<SkData>> media_states;
  const bool canvas_video =
      blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode();
  if (canvas_video)
    result.canvas_video.emplace();
  blink::ReachCanvasVideo::Inventory canvas_inventory;
  size_t canvas_bytes = 0;
  for (blink::Frame* tree_frame = frame.Get(); tree_frame;
       tree_frame = tree_frame->Tree().TraverseNext(frame.Get())) {
    if (!IsInlinePaintFrame(frame.Get(), tree_frame)) {
      continue;
    }
    auto* media_document =
        blink::To<blink::LocalFrame>(tree_frame)->GetDocument();
    if (!media_document) {
      continue;
    }
    ObserveMediaState(media_document->domWindow());
    for (auto& node :
         blink::FlatTreeTraversal::DescendantsOf(*media_document)) {
      if (auto* canvas = blink::DynamicTo<blink::HTMLCanvasElement>(node);
          canvas && canvas_video) {
        if (canvas->IsOffscreenCanvasRegistered()) {
          result.error = "Worker OffscreenCanvas video is unsupported";
          return result;
        }
        auto* context = canvas->RenderingContext();
        if (!context || !context->IsWebGL() || context->isContextLost())
          continue;
        if (web_frame->Parent() || !context->CheckpointReachCanvasVideo()) {
          const auto* snapshot = context->GetReachCanvasVideo();
          result.error =
              snapshot ? snapshot->error() : "WebGL video snapshot unavailable";
          return result;
        }
        const auto& snapshot = *context->GetReachCanvasVideo();
        if (!canvas_inventory.Add(snapshot.width(), snapshot.height()) ||
            snapshot.rgba().empty() ||
            snapshot.rgba().size() > 16 * 1024 * 1024 - canvas_bytes) {
          result.error = "WebGL video inventory budget exceeded";
          return result;
        }
        canvas_bytes += snapshot.rgba().size();
        result.canvas_video->push_back(
            {static_cast<uint32_t>(canvas->GetDomNodeId()),
             document->Token().ToString(),
             base::NumberToString(snapshot.stream()),
             base::NumberToString(snapshot.generation()),
             base::NumberToString(snapshot.sequence()), snapshot.width(),
             snapshot.height(), snapshot.rgba()});
        if (dependencies.media_ids.contains(canvas->GetDomNodeId())) {
          base::DictValue state;
          state.Set("id", canvas->GetDomNodeId());
          state.Set("kind", "webgl-video");
          state.Set("stream", base::NumberToString(snapshot.stream()));
          state.Set("generation", base::NumberToString(snapshot.generation()));
          state.Set("width", snapshot.width());
          state.Set("height", snapshot.height());
          auto json = base::WriteJson(state);
          if (!json) {
            result.error = "WebGL video state serialization failed";
            return result;
          }
          media_states.emplace(
              canvas->GetDomNodeId(),
              SkData::MakeWithCopy(json->data(), json->size()));
        }
        continue;
      }
      if (auto* canvas = blink::DynamicTo<blink::HTMLCanvasElement>(node);
          canvas &&
          blink::Canvas2DBitmapProvider::IsReachWebGLCommandMode() &&
          dependencies.media_ids.contains(canvas->GetDomNodeId())) {
        auto* context = canvas->RenderingContext();
        if (context && context->IsWebGL()) {
          auto* journal = context->GetReachWebGLJournal();
          const auto json = journal ? journal->State() : std::string();
          if (json.empty() || json.size() > 4096) {
            result.error = "WebGL command state unavailable or exceeds budget";
            return result;
          }
          media_states.emplace(
              canvas->GetDomNodeId(),
              SkData::MakeWithCopy(json.data(), json.size()));
        }
        continue;
      }
      auto* media = blink::DynamicTo<blink::HTMLMediaElement>(node);
      if (!media)
        continue;
      // Video state is only replayed where its marker painted. Unpainted
      // videos (e.g. a long feed outside the paint cull rect) are not in this
      // picture, so they need neither transport support nor media state.
      // Audio has no marker and always does.
      if (media->IsHTMLVideoElement() &&
          !dependencies.media_ids.contains(media->GetDomNodeId())) {
        continue;
      }
      const auto& url = media->currentSrc();
      // Replayable media is a same-frame http(s)/data URL, or a same-thread
      // MediaSource whose operations are journaled. Anything else (encrypted
      // media, MediaStream, MediaSource in workers or out-of-process frames,
      // other URL schemes, child-frame URLs) paints as an empty video
      // rather than blocking the page.
      const bool inline_frame = !web_frame->Parent() && tree_frame == frame.Get();
      blink::ReachMediaJournal* journal = nullptr;
      bool blank = false;
      if (blink::HTMLMediaElementEncryptedMedia::mediaKeys(*media) ||
          std::visit([](auto* object) { return object != nullptr; },
                     media->GetSrcObjectVariant())) {
        blank = true;
      } else if (media->HasMediaSource()) {
        journal = blink::ReachMediaJournal::ForElement(*media);
        blank = !journal || !journal->usable() || web_frame->Parent();
      } else if (!url.IsEmpty() &&
                 ((!url.ProtocolIs("http") && !url.ProtocolIs("https") &&
                   !url.ProtocolIsData()) ||
                  !inline_frame)) {
        blank = true;
      }
      if (blank) {
        journal = nullptr;
      }
      // A painted video without a source (e.g. lazily loaded) still needs its
      // state so the client can match the marker; it has no resource to route.
      // Audio without a replayable source has nothing to replay.
      if ((blank || (url.IsEmpty() && !journal)) &&
          !media->IsHTMLVideoElement())
        continue;
      if (!std::isfinite(media->currentTime()) ||
          !std::isfinite(media->playbackRate()) ||
          media->playbackRate() < 0.25 || media->playbackRate() > 4) {
        result.error = "Media playback state is invalid";
        return result;
      }
      base::DictValue state;
      state.Set("id", media->GetDomNodeId());
      state.Set("kind", media->IsHTMLVideoElement() ? "video" : "audio");
      std::string resource;
      if (journal) {
        resource = base::HexEncode(crypto::hash::Sha256(
            "mse:" + base::NumberToString(journal->id())));
      } else if (!blank && !url.IsEmpty()) {
        resource = base::HexEncode(crypto::hash::Sha256(url.GetString().Utf8()));
      }
      state.Set("resource", resource);
      state.Set("time", media->currentTime());
      state.Set("paused", media->paused());
      state.Set("ended", media->ended());
      state.Set("rate", media->playbackRate());
      state.Set("volume", media->volume());
      state.Set("muted", media->muted());
      state.Set("loop", media->Loop());
      if (journal) {
        // The client replays the journal through its tab's /mse route up to
        // this position.
        state.Set("mse", base::NumberToString(journal->id()));
        state.Set("mseSeq", base::NumberToString(journal->next()));
      } else if (!blank && url.ProtocolIs("https") &&
                 !url.HasFragmentIdentifier() && url.User().empty() &&
                 url.Pass().empty() && url.GetString().length() <= 512) {
        // Like image sources, an exact HTTPS URL lets the client stream the
        // original through the identity-free, cacheable /server route.
        state.Set("source", url.GetString().Utf8());
      }
      auto json = base::WriteJson(state);
      if (!json || json->size() > 4096) {
        result.error = "Media state serialization failed";
        return result;
      }
      media_states.emplace(media->GetDomNodeId(),
                           SkData::MakeWithCopy(json->data(), json->size()));
    }
  }
  CaptureTimer::Get().Mark("collect");
  if (known) {
    auto incremental =
        CaptureIncrementalPicture(frame.Get(), bounds, frame_pictures,
                                  media_states, *known, sink, known_originals);
    incremental.canvas_video = std::move(result.canvas_video);
    return incremental;
  }
  PaintSerialization state;
  state.sink = sink;
  state.media_ids = dependencies.culled_paint_ids;
  state.culled_paint_ids = dependencies.culled_paint_ids;
  for (const auto& [id, picture] : frame_pictures) {
    state.media_ids.insert(id);
  }
  for (const auto& [id, data] : media_states) {
    state.media_ids.insert(id);
  }
  // Validate shader-owned records too: playback callbacks do not visit the
  // pictures cached inside PaintShader.
  if (!CheckPaintRecord(&state, recording, 0)) {
    result.error = state.error;
    return result;
  }
  cc::PlaybackCallbacks callbacks;
  callbacks.custom_callback = base::BindRepeating(
      [](const std::map<uint32_t, sk_sp<SkData>>* states,
         const std::map<uint32_t, sk_sp<SkPicture>>* frames,
         PaintSerialization* state, SkCanvas* canvas, uint32_t id) {
        if (state->culled_paint_ids.contains(id) &&
            canvas->quickReject(SkRect::MakeWH(1, 1))) {
          return;
        }
        const auto child = frames->find(id);
        if (child != frames->end()) {
          SkAutoCanvasRestore restore(canvas, true);
          const auto bounds = child->second->cullRect();
          canvas->scale(1 / bounds.width(), 1 / bounds.height());
          // Flatten child commands so nested pictures do not embed the same
          // original fonts repeatedly in independent resource tables.
          child->second->playback(canvas);
          return;
        }
        if (!states->contains(id)) {
          state->error = "Unknown media paint identity";
          return;
        }
        const auto text = base::NumberToString(id);
        auto data = SkData::MakeWithCopy(text.data(), text.size());
        canvas->drawAnnotation(SkRect::MakeWH(1, 1), "Reach.Video.v1",
                               data.get());
      }, base::Unretained(&media_states), base::Unretained(&frame_pictures),
      base::Unretained(&state));
  auto picture = recording.ToSkPicture(
      SkRect::MakeWH(bounds.width(), bounds.height()), nullptr, callbacks);
  if (!state.error.empty() || !picture) {
    result.error =
        state.error.empty() ? "Skia picture recording failed" : state.error;
    return result;
  }
  SkPictureRecorder media_recorder;
  const auto viewport = SkRect::MakeWH(bounds.width(), bounds.height());
  auto* media_canvas = media_recorder.beginRecording(viewport);
  if (!DrawImageStates(media_canvas, state.image_states, &state.error)) {
    result.error = state.error;
    return result;
  }
  for (const auto& [id, data] : media_states) {
    media_canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.MediaState.v1",
                                 data.get());
  }
  picture->playback(media_canvas);
  picture = FinishViewportPicture(&media_recorder, viewport);
  SkSerialProcs procs;
  procs.fImageProc = &SerializeOriginalImage;
  procs.fImageCtx = &state;
  procs.fTypefaceProc = &SerializeOriginalTypeface;
  procs.fTypefaceCtx = &state;
  BoundedPaintStream stream(kMaxPictureBytes);
  picture->serialize(&stream, &procs);
  if (!state.error.empty() || stream.failed()) {
    result.error = state.error.empty() ? "Serialized picture budget exceeded"
                                       : state.error;
    return result;
  }
  result.picture = stream.TakeBytes();
  if (result.picture.size() + state.bytes + state.font_bytes +
          state.identity_bytes > kMaxPaintBytes) {
    result.error = "Aggregate picture and original resource budget exceeded";
    return result;
  }
  result.resources = std::move(state.originals);
  if (result.picture.empty()) {
    result.error = "Skia produced an empty picture";
    return result;
  }
  result.width = bounds.width();
  result.height = bounds.height();
  return result;
}

// A successful local-root capture completes a command-only visual update.
// Failed captures and isolated same-process child pictures complete nothing.
void CompleteDrawingCapture(blink::WebLocalFrame* web_frame,
                            const std::string& expected_document,
                            const CefDrawingCommands& result) {
  if (!blink::Canvas2DBitmapProvider::IsReachCommandMode() ||
      !result.error.empty() || result.picture.empty()) {
    return;
  }
  auto* local = blink::To<blink::WebLocalFrameImpl>(web_frame);
  auto* frame = local->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetDocument() ||
      frame->GetDocument()->Token().ToString() != expected_document ||
      frame != &frame->LocalFrameRoot()) {
    return;
  }
  if (auto* widget = local->FrameWidgetImpl()) {
    widget->DidCompleteReachCommandCapture();
  }
}

}  // namespace

CefDrawingCommands CaptureDrawingCommands(
    blink::WebLocalFrame* web_frame, const std::string& expected_document,
    const std::map<std::string, std::vector<uint8_t>>& children,
    const std::map<std::string, std::vector<uint8_t>>& resources,
    const std::function<void(const std::string&, const void*, size_t)>& sink) {
  auto result = CaptureDrawingCommandsImpl(
      web_frame, expected_document, children, resources, sink, nullptr);
  CompleteDrawingCapture(web_frame, expected_document, result);
  return result;
}

CefDrawingCommands CaptureIncrementalDrawingCommands(
    blink::WebLocalFrame* web_frame, const std::string& expected_document,
    const std::map<std::string, std::vector<uint8_t>>& children,
    const std::map<std::string, std::vector<uint8_t>>& resources,
    const std::vector<uint8_t>& known,
    const std::function<void(const std::string&, const void*, size_t)>& sink,
    const std::set<std::string>* known_originals) {
  if (web_frame->Parent()) {
    CefDrawingCommands result;
    result.error = "Incremental capture requires the root frame";
    return result;
  }
  auto result = CaptureDrawingCommandsImpl(
      web_frame, expected_document, children, resources, sink, &known,
      known_originals);
  CompleteDrawingCapture(web_frame, expected_document, result);
  return result;
}

CefDrawingCommands CaptureRasterFallback(
    blink::WebLocalFrame* web_frame, const std::string& expected_document,
    const std::map<std::string, std::vector<uint8_t>>& children) {
  CefDrawingCommands result;
  if (!blink::Canvas2DBitmapProvider::IsReachCommandMode()) {
    result.error = "Raster fallback requires a command-only runtime";
    return result;
  }
  auto* frame = blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->View() ||
      !frame->GetDocument() ||
      frame->GetDocument()->Token().ToString() != expected_document) {
    result.error = "Raster fallback document is detached or changed";
    return result;
  }
  frame->View()->UpdateAllLifecyclePhasesExceptPaint(
      blink::DocumentUpdateReason::kInspector);
  const auto bounds = frame->View()->LayoutViewport()->VisibleContentRect(
      blink::kIncludeScrollbars);
  PaintSerialization dependencies;
  dependencies.raster = true;
  auto recording = CaptureRequiredPaint(web_frame, bounds, &dependencies);
  if (!recording) {
    result.error = dependencies.error;
    return result;
  }
  std::map<uint32_t, sk_sp<SkImage>> images;
  size_t child_bytes = 0;
  uint64_t child_pixels = dependencies.image_pixels;
  for (auto* child = frame->Tree().TraverseNext(frame); child;
       child = child->Tree().TraverseNext(frame)) {
    if (!IsComposedChildFrame(frame, child)) {
      continue;
    }
    auto* owner = child->OwnerLayoutObject();
    auto* element = owner
        ? blink::DynamicTo<blink::HTMLFrameOwnerElement>(owner->GetNode())
        : nullptr;
    if (!element ||
        !dependencies.media_ids.contains(element->GetDomNodeId()))
      continue;
    const auto found = children.find(DrawingFrameIdentity(child));
    if (found == children.end() ||
        (child_bytes += found->second.size()) >
            kMaxPaintBytes - dependencies.record_bytes) {
      result.error = "Raster fallback missing child picture or byte budget exceeded";
      return result;
    }
    auto data = SkData::MakeWithCopy(found->second.data(), found->second.size());
    auto image = SkImages::DeferredFromEncodedData(std::move(data));
    if (!image || image->width() <= 0 || image->height() <= 0 ||
        image->width() > 4096 || image->height() > 4096 ||
        (child_pixels += uint64_t(image->width()) * image->height()) >
            kMaxDecodedImagePixels) {
      result.error = "Raster fallback child image exceeds decoded pixel budget";
      return result;
    }
    images.emplace(element->GetDomNodeId(), std::move(image));
  }
  auto surface = SkSurfaces::Raster(
      SkImageInfo::MakeN32Premul(bounds.width(), bounds.height()));
  if (!surface) {
    result.error = "Raster fallback surface allocation failed";
    return result;
  }
  surface->getCanvas()->clear(SK_ColorWHITE);
  cc::PlaybackCallbacks callbacks;
  callbacks.custom_callback = base::BindRepeating(
      [](const std::map<uint32_t, sk_sp<SkImage>>* images,
         PaintSerialization* state, SkCanvas* canvas, uint32_t id) {
        if (state->culled_paint_ids.contains(id) &&
            canvas->quickReject(SkRect::MakeWH(1, 1))) return;
        const auto found = images->find(id);
        if (found == images->end()) {
          state->error = "Raster fallback encountered unsupported custom paint";
          return;
        }
        canvas->drawImageRect(found->second, SkRect::MakeWH(1, 1),
                              SkSamplingOptions(), nullptr);
      }, base::Unretained(&images), base::Unretained(&dependencies));
  auto picture = recording->ToSkPicture(
      SkRect::MakeWH(bounds.width(), bounds.height()), nullptr, callbacks);
  if (!picture || !dependencies.error.empty()) {
    result.error = dependencies.error.empty()
        ? "Raster fallback paint picture failed" : dependencies.error;
    return result;
  }
  picture->playback(surface->getCanvas());
  BoundedPaintStream stream(kMaxPictureBytes);
  SkPixmap pixels;
  if (!surface->peekPixels(&pixels) ||
      !SkPngRustEncoder::Encode(&stream, pixels, {}) || stream.failed()) {
    result.error = "Raster fallback PNG encoding failed or exceeded byte budget";
    return result;
  }
  result.width = bounds.width();
  result.height = bounds.height();
  result.picture = stream.TakeBytes();
  return result;
}

cef_dom_node_type_t GetNodeType(const blink::WebNode& node) {
  const blink::Node* web_node = node.ConstUnwrap<blink::Node>();
  switch (web_node->getNodeType()) {
    case blink::Node::kElementNode:
      return DOM_NODE_TYPE_ELEMENT;
    case blink::Node::kAttributeNode:
      return DOM_NODE_TYPE_ATTRIBUTE;
    case blink::Node::kTextNode:
      return DOM_NODE_TYPE_TEXT;
    case blink::Node::kCdataSectionNode:
      return DOM_NODE_TYPE_CDATA_SECTION;
    case blink::Node::kProcessingInstructionNode:
      return DOM_NODE_TYPE_PROCESSING_INSTRUCTIONS;
    case blink::Node::kCommentNode:
      return DOM_NODE_TYPE_COMMENT;
    case blink::Node::kDocumentNode:
      return DOM_NODE_TYPE_DOCUMENT;
    case blink::Node::kDocumentTypeNode:
      return DOM_NODE_TYPE_DOCUMENT_TYPE;
    case blink::Node::kDocumentFragmentNode:
      return DOM_NODE_TYPE_DOCUMENT_FRAGMENT;
  }
  return DOM_NODE_TYPE_UNSUPPORTED;
}

blink::WebString GetNodeName(const blink::WebNode& node) {
  const blink::Node* web_node = node.ConstUnwrap<blink::Node>();
  return web_node->nodeName();
}

blink::WebString CreateNodeMarkup(const blink::WebNode& node) {
  const blink::Node* web_node = node.ConstUnwrap<blink::Node>();
  return blink::CreateMarkup(web_node);
}

bool SetNodeValue(blink::WebNode& node, const blink::WebString& value) {
  blink::Node* web_node = node.Unwrap<blink::Node>();
  web_node->setNodeValue(value);
  return true;
}

v8::MaybeLocal<v8::Value> CallV8Function(v8::Local<v8::Context> context,
                                         v8::Local<v8::Function> function,
                                         v8::Local<v8::Object> receiver,
                                         int argc,
                                         v8::Local<v8::Value> args[],
                                         v8::Isolate* isolate) {
  v8::MaybeLocal<v8::Value> func_rv;

  // Execute the function call using the V8ScriptRunner so that inspector
  // instrumentation works.
  if (auto execution_context = GetExecutionContext(context)) {
    func_rv = blink::V8ScriptRunner::CallFunction(
        function, execution_context, receiver, argc, args, isolate);
  }

  return func_rv;
}

bool IsTextControlElement(const blink::WebElement& element) {
  const blink::Element* web_element = element.ConstUnwrap<blink::Element>();
  return web_element->IsTextControl();
}

v8::Local<v8::Value> ExecuteV8ScriptAndReturnValue(
    const blink::WebString& source,
    const blink::WebString& source_url,
    int start_line,
    v8::Local<v8::Context> context,
    v8::TryCatch& tryCatch,
    v8::Isolate* isolate) {
  if (start_line < 1) {
    start_line = 1;
  }

  blink::LocalFrame* frame = blink::ToLocalFrameIfNotDetached(context);
  if (!frame) {
    return v8::Local<v8::Value>();
  }

  auto* script = blink::ClassicScript::Create(
      source, blink::KURL(source_url), blink::KURL(source_url),
      blink::ScriptFetchOptions(), blink::ScriptSourceLocationType::kInternal,
      blink::SanitizeScriptErrors::kDoNotSanitize, /*cache_handler=*/nullptr,
      blink::TextPosition(blink::OrdinalNumber::FromOneBasedInt(start_line),
                          blink::OrdinalNumber::FromZeroBasedInt(0)));

  // The Rethrow() message is unused due to kDoNotSanitize but it still needs
  // to be non-nullopt for exceptions to be re-thrown as expected.
  auto result = blink::V8ScriptRunner::CompileAndRunScript(
      blink::ScriptState::From(isolate, context), script,
      blink::ExecuteScriptPolicy::kExecuteScriptWhenScriptsDisabled,
      blink::V8ScriptRunner::RethrowErrorsOption::Rethrow(""));

  if (result.GetResultType() ==
      blink::ScriptEvaluationResult::ResultType::kSuccess) {
    return result.GetSuccessValue();
  }

  DCHECK(tryCatch.HasCaught());
  return v8::Local<v8::Value>();
}

v8::MicrotaskQueue* GetMicrotaskQueue(v8::Local<v8::Context> context) {
  if (auto execution_context = GetExecutionContext(context)) {
    return execution_context->GetMicrotaskQueue();
  }
  return nullptr;
}

bool IsScriptForbidden() {
  return blink::ScriptForbiddenScope::IsScriptForbidden();
}

std::unique_ptr<CefObserverRegistration>
RegisterExecutionContextLifecycleStateObserver(
    v8::Local<v8::Context> context,
    CefExecutionContextLifecycleStateObserver* observer) {
  class Observer : public blink::GarbageCollected<Observer>,
                   public blink::ExecutionContextLifecycleStateObserver {
   public:
    Observer(blink::ExecutionContext* execution_context,
             CefExecutionContextLifecycleStateObserver* observer)
        : blink::ExecutionContextLifecycleStateObserver(execution_context),
          observer_(observer) {
      UpdateStateIfNeeded();
    }

    void ContextLifecycleStateChanged(
        blink::mojom::blink::FrameLifecycleState state) override {
      observer_->ContextLifecycleStateChanged(state);
    }

    void ContextDestroyed() override {}

   private:
    CefExecutionContextLifecycleStateObserver* observer_;
  };

  class Registration : public CefObserverRegistration {
   public:
    explicit Registration(blink::Persistent<Observer> observer)
        : observer_(observer) {}

   private:
    blink::Persistent<Observer> observer_;
  };

  return std::make_unique<Registration>(blink::MakeGarbageCollected<Observer>(
      blink::ExecutionContext::From(context), observer));
}

void RegisterURLSchemeAsSupportingFetchAPI(const blink::WebString& scheme) {
  blink::SchemeRegistry::RegisterURLSchemeAsSupportingFetchAPI(scheme);
}

struct CefScriptForbiddenScope::Impl {
  STACK_ALLOCATED_IGNORE()
  blink::ScriptForbiddenScope scope_;
};

CefScriptForbiddenScope::CefScriptForbiddenScope() : impl_(new Impl()) {}

CefScriptForbiddenScope::~CefScriptForbiddenScope() = default;

bool ResponseWasCached(const blink::WebURLResponse& response) {
  return response.ToResourceResponse().WasCached();
}

bool HasPluginFrameOwner(blink::WebLocalFrame* frame) {
  blink::Frame* core_frame = blink::WebFrame::ToCoreFrame(*frame);
  return core_frame->Owner() && core_frame->Owner()->IsPlugin();
}

// Based on WebLocalFrameImpl::StartNavigation which was removed in
// https://crrev.com/de4fc2a5fe.
void StartNavigation(blink::WebLocalFrame* frame,
                     const blink::WebURLRequest& request) {
  DCHECK(!request.IsNull());
  DCHECK(!request.Url().ProtocolIs("javascript"));

  blink::FrameLoadRequest frame_load_request(nullptr,
                                             request.ToResourceRequest());
  blink::Frame* core_frame = blink::WebFrame::ToCoreFrame(*frame);
  blink::To<blink::LocalFrame>(core_frame)
      ->Loader()
      .StartNavigation(frame_load_request, blink::WebFrameLoadType::kStandard);
}

}  // namespace blink_glue
