// Copyright (c) 2012 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/renderer/blink_glue.h"

#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include <cmath>
#include <map>
#include <set>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "crypto/hash.h"
#include "cc/paint/paint_op.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_op_buffer_iterator.h"
#include "cc/paint/paint_record.h"
#include "cc/paint/paint_recorder.h"
#include "cc/paint/paint_shader.h"
#include "cc/trees/scroll_source_type.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
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
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/editing/serializers/serialization.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_state_observer.h"
#include "third_party/blink/renderer/core/frame/frame_owner.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/canvas/html_canvas_element.h"
#include "third_party/blink/renderer/core/html/media/html_media_element.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/layout/hit_test_location.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/video_painter.h"
#include "third_party/blink/renderer/modules/encryptedmedia/html_media_element_encrypted_media.h"
#include "third_party/blink/renderer/core/script/classic_script.h"
#include "third_party/blink/renderer/core/scroll/scrollable_area.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/v8_binding.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_response.h"
#include "third_party/blink/renderer/platform/loader/fetch/script_fetch_options.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "third_party/skia/include/core/SkSerialProcs.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkTypeface.h"
#undef LOG

#include "base/logging.h"

namespace blink_glue {

namespace {

constexpr size_t kMaxPictureBytes = 8 * 1024 * 1024;
constexpr size_t kMaxResourceBytes = 4 * 1024 * 1024;
constexpr size_t kMaxPaintBytes = 32 * 1024 * 1024;
constexpr size_t kMaxPaintOps = 100000;
constexpr size_t kMaxLayoutObjects = 100000;
constexpr size_t kMaxResources = 64;
constexpr size_t kMaxFilterNodes = 1024;
constexpr size_t kMaxFilterDepth = 32;
constexpr size_t kMaxRecordDepth = 32;
constexpr size_t kMaxRecordShaders = 1024;
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

struct PaintSerialization {
  std::string error;
  size_t resources = 0;
  size_t bytes = 0;
  uint64_t image_pixels = 0;
  size_t filter_nodes = 0;
  size_t paint_ops = 0;
  size_t record_bytes = 0;
  size_t record_shaders = 0;
  std::set<uint32_t> media_ids;

  bool AddResource(size_t size) {
    if (!error.empty()) {
      return false;
    }
    if (size == 0 || size > kMaxResourceBytes - bytes ||
        resources >= kMaxResources) {
      error = "Original resource budget exceeded or resource unavailable";
      return false;
    }
    ++resources;
    bytes += size;
    return true;
  }
};

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
  if (state->error.empty() &&
      pixels > kMaxDecodedImagePixels - state->image_pixels) {
    state->error = "Decoded image pixel budget exceeded";
  }
  if (state->error.empty()) {
    auto encoded = image->refEncodedData();
    if (encoded && state->AddResource(encoded->size())) {
      state->image_pixels += pixels;
      return encoded;
    }
    if (state->error.empty()) {
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
    int collection_index = 0;
    auto original = face->openStream(&collection_index);
    if (original && original->getLength() > 0 &&
        original->getLength() <= kMaxResourceBytes - state->bytes) {
      BoundedPaintStream stream(kMaxResourceBytes - state->bytes);
      if (face->serialize(&stream,
                          SkTypeface::SerializeBehavior::kDoIncludeData) &&
          !stream.failed()) {
        auto bytes = stream.TakeBytes();
        if (state->AddResource(bytes.size())) {
          return SkData::MakeWithCopy(bytes.data(), bytes.size());
        }
      }
    }
    if (state->error.empty()) {
      state->error = "Original font unavailable or resource budget exceeded";
    }
  }
  return SkData::MakeEmpty();
}

bool CheckPaintImage(PaintSerialization* state, const cc::PaintImage& image) {
  if (image.IsTextureBacked() || image.IsDeferredPaintRecord()) {
    state->error = "Texture-backed or deferred image requires command support";
    return false;
  }
  if (image.animation_type() == cc::PaintImage::AnimationType::kVideo) {
    state->error = "Video image requires original media transport";
    return false;
  }
  // Animation decoding and color conversion belong to the client. Multipart
  // updates retain the current part's original bytes, checked by serialization.
  if (image.FrameCount() > 256) {
    state->error = "Animated image frame count exceeds client decode budget";
    return false;
  }
  return true;
}

bool CheckPaintFilter(PaintSerialization* state,
                      const cc::PaintFilter* filter,
                      size_t depth = 0) {
  if (!state->error.empty()) {
    return false;
  }
  if (!filter) {
    return true;
  }
  if (depth >= kMaxFilterDepth || ++state->filter_nodes > kMaxFilterNodes) {
    state->error = "Image-filter graph budget exceeded";
    return false;
  }
  const auto sigma_ok = [](float sigma) {
    return std::isfinite(sigma) && sigma >= 0 && sigma <= 128;
  };
  switch (filter->type()) {
    case cc::PaintFilter::Type::kBlur: {
      const auto* blur = static_cast<const cc::BlurPaintFilter*>(filter);
      if (!sigma_ok(blur->sigma_x()) || !sigma_ok(blur->sigma_y())) {
        state->error = "Image-filter blur budget exceeded";
        return false;
      }
      break;
    }
    case cc::PaintFilter::Type::kDropShadow: {
      const auto* shadow =
          static_cast<const cc::DropShadowPaintFilter*>(filter);
      if (!sigma_ok(shadow->sigma_x()) || !sigma_ok(shadow->sigma_y()) ||
          !std::isfinite(shadow->dx()) || !std::isfinite(shadow->dy()) ||
          std::abs(shadow->dx()) > 4096 || std::abs(shadow->dy()) > 4096) {
        state->error = "Image-filter shadow budget exceeded";
        return false;
      }
      break;
    }
    case cc::PaintFilter::Type::kColorFilter:
      break;
    case cc::PaintFilter::Type::kCompose: {
      const auto* compose =
          static_cast<const cc::ComposePaintFilter*>(filter);
      return CheckPaintFilter(state, compose->outer().get(), depth + 1) &&
             CheckPaintFilter(state, compose->inner().get(), depth + 1);
    }
    default:
      state->error = "Image-filter " +
                     cc::PaintFilter::TypeToString(filter->type()) +
                     " requires command support";
      return false;
  }
  // These filters only transform their input; they cannot hide image, shader
  // or paint-record resources outside the original-resource serializer.
  return CheckPaintFilter(
      state, static_cast<const cc::OneInputPaintFilter*>(filter)->input().get(),
      depth + 1);
}

bool CheckPaintRecord(PaintSerialization* state,
                      const cc::PaintRecord& record,
                      size_t depth);

const cc::PaintOp* CheckPaintOperation(PaintSerialization* state,
                                      const cc::PaintOp& operation,
                                      size_t depth) {
  if (!state->error.empty()) {
    return nullptr;
  }
  if (operation.GetType() == cc::PaintOpType::kCustomData &&
      !state->media_ids.contains(
          static_cast<const cc::CustomDataOp&>(operation).id)) {
    state->error = "Subframe/compositor transaction requires command support";
    return nullptr;
  }
  if (operation.GetType() == cc::PaintOpType::kDrawRecord &&
      !CheckPaintRecord(
          state, static_cast<const cc::DrawRecordOp&>(operation).record,
          depth + 1)) {
    return nullptr;
  }
  if (operation.GetType() == cc::PaintOpType::kDrawSkottie ||
      operation.GetType() == cc::PaintOpType::kDrawSlug) {
    state->error = "Skottie or GPU text requires command support";
    return nullptr;
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
    if (!CheckPaintFilter(state, layer.backdrop_filter.get())) {
      return nullptr;
    }
    for (const auto& filter : layer.filters) {
      if (!CheckPaintFilter(state, filter.get())) {
        return nullptr;
      }
    }
  }
  if (operation.IsPaintOpWithFlags()) {
    const auto& flags =
        static_cast<const cc::PaintOpWithFlags&>(operation).flags;
    if (!CheckPaintFilter(state, flags.getImageFilter().get())) {
      return nullptr;
    }
    if (const auto* shader = flags.getShader()) {
      if (!shader->IsValid()) {
        state->error = "Invalid paint shader";
        return nullptr;
      }
      if (shader->shader_type() == cc::PaintShader::Type::kPaintRecord) {
        const auto* record = shader->paint_record();
        const auto tile = shader->tile();
        if (!record || !tile.isFinite() || tile.isEmpty() ||
            tile.width() > 4096 || tile.height() > 4096 ||
            ++state->record_shaders > kMaxRecordShaders) {
          state->error = "Paint-record shader tile or count exceeds budget";
          return nullptr;
        }
        if (!CheckPaintRecord(state, *record, depth + 1)) {
          return nullptr;
        }
      }
      if (shader->shader_type() == cc::PaintShader::Type::kImage &&
          !CheckPaintImage(state, shader->paint_image())) {
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
    if (++state->paint_ops > kMaxPaintOps) {
      state->error = "Nested paint operation budget exceeded";
      return false;
    }
    if (!CheckPaintOperation(state, operation, depth)) {
      return false;
    }
  }
  return true;
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
  const auto type = kind == 0 ? blink::WebInputEvent::Type::kMouseMove :
      kind == 1 ? blink::WebInputEvent::Type::kMouseLeave :
      up ? blink::WebInputEvent::Type::kMouseUp : blink::WebInputEvent::Type::kMouseDown;
  blink::WebMouseEvent event(type, DrawingInputFlags(modifiers), base::TimeTicks::Now());
  event.SetPositionInWidget(x, y);
  event.SetPositionInScreen(x, y);
  event.button = kind != 2 ? blink::WebPointerProperties::Button::kNoButton :
      button == 0 ? blink::WebPointerProperties::Button::kLeft :
      button == 1 ? blink::WebPointerProperties::Button::kMiddle :
                    blink::WebPointerProperties::Button::kRight;
  event.click_count = count;
  if (kind == 0) frame->GetEventHandler().HandleMouseMoveEvent(event, {}, {});
  else if (kind == 1) frame->GetEventHandler().HandleMouseLeaveEvent(event);
  else if (up) frame->GetEventHandler().HandleMouseReleaseEvent(event);
  else frame->GetEventHandler().HandleMousePressEvent(event);
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
  event.delta_units = ui::ScrollGranularity::kScrollByPrecisePixel;
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

CefDrawingCommands CaptureDrawingCommands(blink::WebLocalFrame* web_frame) {
  CefDrawingCommands result;
  blink::Persistent<blink::LocalFrame> frame =
      blink::To<blink::WebLocalFrameImpl>(web_frame)->GetFrame();
  if (!frame || !frame->IsAttached() || !frame->GetPage() ||
      !frame->GetDocument() || !frame->View()) {
    result.error = "Frame is detached or not ready";
    return result;
  }
  blink::Persistent<blink::Document> document = frame->GetDocument();
  blink::Persistent<blink::Page> page = frame->GetPage();
  page->Animator().ServiceScriptedAnimations(base::TimeTicks::Now());
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during animation";
    return result;
  }
  page->Animator().UpdateAllLifecyclePhasesExceptPaint(
      *frame, blink::DocumentUpdateReason::kInspector);
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during layout";
    return result;
  }
  const auto* root = document->GetLayoutView();
  if (!root) {
    result.error = "Document has no layout";
    return result;
  }
  size_t objects = 0;
  for (const blink::LayoutObject* object = root; object;
       object = object->NextInPreOrder(root)) {
    if (++objects > kMaxLayoutObjects) {
      result.error = "Layout object budget exceeded";
      return result;
    }
    const auto* node = object->GetNode();
    if (object->IsLayoutEmbeddedContent() ||
        (node && blink::IsA<blink::HTMLCanvasElement>(node))) {
      result.error =
          "Canvas or embedded content requires command support";
      return result;
    }
  }
  const auto bounds = frame->View()->LayoutViewport()->VisibleContentRect(
      blink::kIncludeScrollbars);
  if (bounds.IsEmpty() || bounds.width() > 4096 || bounds.height() > 4096 ||
      static_cast<uint64_t>(bounds.width()) * bounds.height() >
          kMaxViewportPixels) {
    result.error = "Capture viewport is empty or exceeds budget";
    return result;
  }
  cc::PaintRecorder recorder;
  auto* canvas = recorder.beginRecording();
  size_t media_count = 0;
  std::map<uint32_t, sk_sp<SkData>> media_states;
  for (auto& node : blink::FlatTreeTraversal::DescendantsOf(*document)) {
    auto* media = blink::DynamicTo<blink::HTMLMediaElement>(node);
    if (!media)
      continue;
    const auto& url = media->currentSrc();
    if (media->HasMediaSource() ||
        blink::HTMLMediaElementEncryptedMedia::mediaKeys(*media) ||
        std::visit([](auto* object) { return object != nullptr; },
                   media->GetSrcObjectVariant()) ||
        (!url.IsEmpty() && !url.ProtocolIs("http") && !url.ProtocolIs("https") &&
         !url.ProtocolIsData())) {
      result.error = "MediaSource, MediaStream or encrypted media requires transport support";
      return result;
    }
    if (++media_count > 16 || !std::isfinite(media->currentTime()) ||
        !std::isfinite(media->playbackRate()) ||
        media->playbackRate() < 0.25 || media->playbackRate() > 4) {
      result.error = "Media count or playback state exceeds budget";
      return result;
    }
    base::DictValue state;
    state.Set("id", media->GetDomNodeId());
    state.Set("kind", media->IsHTMLVideoElement() ? "video" : "audio");
    state.Set("resource", url.IsEmpty() ? "" :
        base::HexEncode(crypto::hash::Sha256(url.GetString().Utf8())));
    state.Set("time", media->currentTime());
    state.Set("paused", media->paused());
    state.Set("ended", media->ended());
    state.Set("rate", media->playbackRate());
    state.Set("volume", media->volume());
    state.Set("muted", media->muted());
    state.Set("loop", media->Loop());
    auto json = base::WriteJson(state);
    if (!json || json->size() > 4096) {
      result.error = "Media state serialization failed";
      return result;
    }
    media_states.emplace(media->GetDomNodeId(),
                         SkData::MakeWithCopy(json->data(), json->size()));
  }
  canvas->translate(-bounds.x(), -bounds.y());
  const bool previous_media_capture = blink::SetReachMediaCapture(true);
  base::ScopedClosureRunner restore_media_capture(base::BindOnce(
      [](bool previous) { blink::SetReachMediaCapture(previous); },
      previous_media_capture));
  if (!web_frame->CapturePaintPreview(
          bounds, canvas, /*include_linked_destinations=*/false,
          /*skip_accelerated_content=*/false, /*allow_scrollbars=*/true)) {
    result.error = "Blink paint recording failed";
    return result;
  }
  auto recording = recorder.finishRecordingAsPicture();
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get()) {
    result.error = "Frame changed during paint recording";
    return result;
  }
  if (recording.bytes_used() > kMaxPaintBytes ||
      recording.total_op_count() > kMaxPaintOps) {
    result.error = "Paint record budget exceeded";
    return result;
  }
  PaintSerialization state;
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
         PaintSerialization* state, SkCanvas* canvas, uint32_t id) {
        if (!states->contains(id)) {
          state->error = "Unknown media paint identity";
          return;
        }
        const auto text = base::NumberToString(id);
        auto data = SkData::MakeWithCopy(text.data(), text.size());
        canvas->drawAnnotation(SkRect::MakeWH(1, 1), "Reach.Video.v1",
                               data.get());
      }, base::Unretained(&media_states), base::Unretained(&state));
  auto picture = recording.ToSkPicture(
      SkRect::MakeWH(bounds.width(), bounds.height()), nullptr, callbacks);
  if (!state.error.empty() || !picture) {
    result.error =
        state.error.empty() ? "Skia picture recording failed" : state.error;
    return result;
  }
  SkPictureRecorder media_recorder;
  auto* media_canvas = media_recorder.beginRecording(picture->cullRect());
  for (const auto& [id, data] : media_states) {
    media_canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.MediaState.v1",
                                 data.get());
  }
  media_canvas->drawPicture(picture);
  picture = media_recorder.finishRecordingAsPicture();
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
  if (result.picture.empty()) {
    result.error = "Skia produced an empty picture";
    return result;
  }
  result.width = bounds.width();
  result.height = bounds.height();
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
