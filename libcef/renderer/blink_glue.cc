// Copyright (c) 2012 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/renderer/blink_glue.h"

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
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
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/media/html_media_element.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/layout/hit_test_location.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/paint/paint_flags.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/video_painter.h"
#include "third_party/blink/renderer/core/script/classic_script.h"
#include "third_party/blink/renderer/core/scroll/scrollable_area.h"
#include "third_party/blink/renderer/modules/encryptedmedia/html_media_element_encrypted_media.h"
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
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "third_party/skia/include/core/SkSerialProcs.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/encode/SkPngRustEncoder.h"
#include "third_party/skia/src/core/SkFontDescriptor.h"
#include "third_party/skia/src/core/SkReachResource.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/gfx/geometry/skia_conversions.h"
#undef LOG

#include "base/logging.h"

namespace blink_glue {

namespace {

constexpr size_t kMaxPictureBytes = 8 * 1024 * 1024;
constexpr size_t kMaxResourceBytes = 4 * 1024 * 1024;
constexpr size_t kMaxPaintBytes = 32 * 1024 * 1024;
constexpr size_t kMaxPaintOps = 100000;
constexpr size_t kMaxLayoutObjects = 100000;
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
  bool raster = false;
  std::function<void(const std::string&, const void*, size_t)> sink;
  std::set<std::string> original_ids;
  size_t identity_bytes = 0;
  size_t resources = 0;
  size_t bytes = 0;
  uint64_t image_pixels = 0;
  size_t filter_nodes = 0;
  size_t paint_ops = 0;
  size_t record_bytes = 0;
  size_t record_shaders = 0;
  std::set<uint32_t> media_ids;
  std::set<uint32_t> culled_paint_ids;
  bool collect_paint_ids = false;
  std::map<std::string, std::vector<uint8_t>> originals;
  size_t font_bytes = 0;
  size_t fonts = 0;
  size_t typefaces = 0;

  bool Export(uint8_t kind, const sk_sp<const SkData>& data, sk_reach::Hash* hash) {
    if (!data || !data->size()) return false;
    *hash = crypto::hash::Sha256(std::string_view(
        static_cast<const char*>(data->data()), data->size()));
    const std::string key = (kind == sk_reach::kImage ? "image:" : "font:") +
                            sk_reach::Hex(*hash);
    if (original_ids.contains(key)) return true;
    if (kind == sk_reach::kImage) {
      if (!AddResource(data->size())) return false;
    } else {
      if (data->size() > sk_reach::kFontBytes - font_bytes) {
        error = "Original font resource byte budget exceeded";
        return false;
      }
      ++fonts;
      font_bytes += data->size();
    }
    identity_bytes += key.size() + 64;
    if (bytes + font_bytes + identity_bytes > kMaxPaintBytes) {
      error = "Aggregate original resource budget exceeded";
      return false;
    }
    original_ids.insert(key);
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
    if (size == 0 || size > kMaxResourceBytes - bytes) {
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
    sk_reach::Hash hash;
    if (encoded && state->Export(sk_reach::kImage, encoded, &hash)) {
      state->image_pixels += pixels;
      return sk_reach::Write(sk_reach::kImage, hash, encoded->size());
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
    if (!original || original->getLength() == 0) {
      state->error = "Original font stream unavailable";
    } else if (original->getLength() > sk_reach::kFontBytes) {
      state->error = "Original font resource byte budget exceeded";
    } else {
      auto data = SkData::MakeFromStream(original.get(), original->getLength());
      sk_reach::Hash hash;
      if (state->Export(sk_reach::kFont, data, &hash)) {
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
          const auto bytes = stream.TakeBytes();
          auto meta = SkData::MakeWithCopy(bytes.data(), bytes.size());
          return sk_reach::Write(sk_reach::kFont, hash, data->size(), meta.get());
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

bool CheckPaintImage(PaintSerialization* state, const cc::PaintImage& image) {
  if (state->raster) {
    const uint64_t pixels = uint64_t(std::max(0, image.width())) *
                            std::max(0, image.height());
    if (image.IsTextureBacked() || image.IsDeferredPaintRecord()) {
      state->error = "Raster fallback cannot read texture-backed or deferred paint";
      return false;
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
    case cc::PaintFilter::Type::kXfermode: {
      const auto* blend =
          static_cast<const cc::XfermodePaintFilter*>(filter);
      return CheckPaintFilter(state, blend->background().get(), depth + 1) &&
             CheckPaintFilter(state, blend->foreground().get(), depth + 1);
    }
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
  if (operation.GetType() == cc::PaintOpType::kCustomData) {
    const auto id = static_cast<const cc::CustomDataOp&>(operation).id;
    if (state->collect_paint_ids) {
      state->media_ids.insert(id);
    } else if (!state->media_ids.contains(id)) {
      state->error = "Subframe/compositor transaction requires command support";
      return nullptr;
    }
  }
  if (operation.GetType() == cc::PaintOpType::kDrawRecord &&
      !CheckPaintRecord(
          state, static_cast<const cc::DrawRecordOp&>(operation).record,
          depth + 1)) {
    return nullptr;
  }
  if (state->raster) {
    if ((operation.GetType() == cc::PaintOpType::kDrawImage &&
         !CheckPaintImage(
             state, static_cast<const cc::DrawImageOp&>(operation).image)) ||
        (operation.GetType() == cc::PaintOpType::kDrawImageRect &&
         !CheckPaintImage(
             state, static_cast<const cc::DrawImageRectOp&>(operation).image)))
      return nullptr;
    if (operation.IsPaintOpWithFlags()) {
      const auto* shader =
          static_cast<const cc::PaintOpWithFlags&>(operation).flags.getShader();
      if (shader && shader->shader_type() == cc::PaintShader::Type::kImage &&
          !CheckPaintImage(state, shader->paint_image()))
        return nullptr;
      if (shader && shader->shader_type() == cc::PaintShader::Type::kPaintRecord &&
          (!shader->paint_record() ||
           !CheckPaintRecord(state, *shader->paint_record(), depth + 1)))
        return nullptr;
    }
    return &operation;
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
  size_t objects = 0;
  std::set<uint32_t> frame_ids;
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
      if (++objects > kMaxLayoutObjects) {
        dependencies->error = "Layout object budget exceeded";
        return std::nullopt;
      }
      if (object->IsLayoutEmbeddedContent()) {
        if (auto* owner = blink::DynamicTo<blink::HTMLFrameOwnerElement>(
                object->GetNode())) {
          frame_ids.insert(owner->GetDomNodeId());
        }
      }
      const auto* element =
          blink::DynamicTo<blink::HTMLCanvasElement>(object->GetNode());
      if (!element || dependencies->raster) {
        continue;
      }
      auto* context = element->RenderingContext();
      if (element->IsOffscreenCanvasRegistered() ||
          (context && !context->IsRenderingContext2D())) {
        dependencies->error =
            "GPU, bitmap or offscreen canvas requires command transport";
        return std::nullopt;
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
    // The normal lifecycle paint, replayed in viewport coordinates.
    const gfx::Rect viewport(bounds.size());
    recording = frame->View()->GetPaintRecord(&viewport);
  }
  dependencies->collect_paint_ids = true;
  if (!CheckPaintRecord(dependencies, recording, 0)) {
    return std::nullopt;
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
                                        bool keyboard, int x, int y) {
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
  if (!bounds.IsEmpty() &&
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
    if (children.size() >= (raster ? 255u : 16u)) {
      result.error =
          "Paint-required child frame count budget exceeded: selected=16, attempted=17";
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

// The skeleton is the normal lifecycle conversion of the local root's paint
// artifact, with each kept chunk's drawings replaced by a placeholder picture
// that serializes as its 16-byte key. Chunks are culled as a whole against the
// viewport expanded by one viewport height above and below.
CefDrawingCommands CaptureIncrementalPicture(
    blink::LocalFrame* frame, const gfx::Rect& bounds,
    const std::set<uint32_t>& frame_ids,
    const std::map<uint32_t, sk_sp<SkPicture>>& frame_pictures,
    const std::map<uint32_t, sk_sp<SkData>>& media_states,
    const std::vector<uint8_t>& known_bytes,
    const std::function<void(const std::string&, const void*, size_t)>& sink) {
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
  std::map<std::tuple<uint64_t, uint32_t, uint32_t>, uint32_t> occurrences;
  bool overflow = false;
  const gfx::Rect cull(0, -bounds.height(), bounds.width(),
                       3 * bounds.height());
  auto skeleton_record = blink::PaintChunksToCcLayer::Convert(
      blink::PaintChunkSubset(frame->View()->GetPaintArtifact()),
      blink::PropertyTreeState::Root(), &cull,
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
        chunks.push_back({MakeChunkKey(chunk.id, occurrence), std::move(items),
                          gfx::RectToSkRect(chunk.drawable_bounds)});
        return marker.ReleaseAsRecord();
      });
  if (overflow) {
    result.error = "Paint chunk reference budget exceeded";
    return result;
  }
  PaintSerialization state;
  state.sink = sink;
  // The skeleton holds only property-state operations and chunk markers.
  state.collect_paint_ids = true;
  if (!CheckPaintRecord(&state, skeleton_record, 0)) {
    result.error = state.error;
    return result;
  }
  for (const auto id : state.media_ids) {
    if (id >= chunks.size()) {
      result.error = "Unknown paint chunk marker";
      return result;
    }
  }
  state.collect_paint_ids = false;
  state.media_ids = frame_ids;
  for (const auto& [id, picture] : frame_pictures)
    state.media_ids.insert(id);
  for (const auto& [id, data] : media_states)
    state.media_ids.insert(id);
  for (const auto& chunk : chunks) {
    if (!CheckPaintRecord(&state, chunk.record, 0)) {
      result.error = state.error;
      return result;
    }
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
  SkPictureRecorder media_recorder;
  auto* media_canvas = media_recorder.beginRecording(picture->cullRect());
  for (const auto& [id, data] : media_states) {
    media_canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.MediaState.v1",
                                 data.get());
  }
  // Replaying into a recorder keeps placeholder references intact.
  picture->playback(media_canvas);
  picture = media_recorder.finishRecordingAsPicture();
  SkSerialProcs procs;
  procs.fImageProc = &SerializeOriginalImage;
  procs.fImageCtx = &state;
  procs.fTypefaceProc = &SerializeOriginalTypeface;
  procs.fTypefaceCtx = &state;
  procs.fPictureProc = &SerializeChunkReference;
  procs.fPictureCtx = &skeleton;
  BoundedPaintStream skeleton_stream(kMaxPictureBytes);
  picture->serialize(&skeleton_stream, &procs);
  if (!state.error.empty() || skeleton_stream.failed()) {
    result.error = state.error.empty() ? "Serialized skeleton budget exceeded"
                                       : state.error;
    return result;
  }
  result.picture = skeleton_stream.TakeBytes();
  if (result.picture.empty()) {
    result.error = "Skia produced an empty skeleton";
    return result;
  }
  // Chunk pictures compose child frames and media markers like the full
  // picture. Child frames outside the capture viewport have no picture; they
  // are omitted, and their chunk changes once they become visible.
  cc::PlaybackCallbacks chunk_callbacks;
  chunk_callbacks.custom_callback = base::BindRepeating(
      [](const std::map<uint32_t, sk_sp<SkData>>* states,
         const std::map<uint32_t, sk_sp<SkPicture>>* frames,
         const std::set<uint32_t>* frame_ids, PaintSerialization* state,
         SkCanvas* canvas, uint32_t id) {
        const auto child = frames->find(id);
        if (child != frames->end()) {
          SkAutoCanvasRestore restore(canvas, true);
          const auto bounds = child->second->cullRect();
          canvas->scale(1 / bounds.width(), 1 / bounds.height());
          child->second->playback(canvas);
          return;
        }
        if (frame_ids->contains(id))
          return;
        if (!states->contains(id)) {
          state->error = "Unknown media paint identity";
          return;
        }
        const auto text = base::NumberToString(id);
        auto data = SkData::MakeWithCopy(text.data(), text.size());
        canvas->drawAnnotation(SkRect::MakeWH(1, 1), "Reach.Video.v1",
                               data.get());
      }, base::Unretained(&media_states), base::Unretained(&frame_pictures),
      base::Unretained(&frame_ids), base::Unretained(&state));
  SkSerialProcs chunk_procs;
  chunk_procs.fImageProc = &SerializeOriginalImage;
  chunk_procs.fImageCtx = &state;
  chunk_procs.fTypefaceProc = &SerializeOriginalTypeface;
  chunk_procs.fTypefaceCtx = &state;
  size_t new_bytes = 0;
  for (const auto index : skeleton.referenced) {
    const auto& chunk = chunks[index];
    auto chunk_picture =
        chunk.record.ToSkPicture(chunk.bounds, nullptr, chunk_callbacks);
    if (!state.error.empty() || !chunk_picture) {
      result.error =
          state.error.empty() ? "Skia chunk recording failed" : state.error;
      return result;
    }
    BoundedPaintStream stream(kMaxPictureBytes);
    chunk_picture->serialize(&stream, &chunk_procs);
    if (!state.error.empty() || stream.failed() || !stream.bytesWritten()) {
      result.error = state.error.empty() ? "Serialized chunk budget exceeded"
                                         : state.error;
      return result;
    }
    auto bytes = stream.TakeBytes();
    const auto hash = crypto::hash::Sha256(bytes);
    result.refs.insert(result.refs.end(), chunk.key.begin(), chunk.key.end());
    result.refs.insert(result.refs.end(), hash.begin(), hash.end());
    const auto found = known.find(chunk.key);
    if (found != known.end() && found->second == hash)
      continue;
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
  if (result.picture.size() + new_bytes + state.bytes + state.font_bytes +
          state.identity_bytes > kMaxPaintBytes) {
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
    const std::vector<uint8_t>* known) {
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
  // Held begin frames also suppress the compositor's idle-period signal.
  // Let Blink schedule idle work normally between command captures.
  static_cast<blink::scheduler::MainThreadSchedulerImpl&>(
      blink::scheduler::WebThreadScheduler::MainThreadScheduler())
      .BeginFrameNotExpectedSoon();
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
  page->Animator().UpdateAllLifecyclePhases(
      *frame, blink::DocumentUpdateReason::kInspector);
  if (!frame->IsAttached() || frame->GetDocument() != document.Get() ||
      frame->GetPage() != page.Get() || !frame->View()) {
    result.error = "Frame changed during animation start";
    return result;
  }
  frame->LocalFrameRoot().View()->RunPostLifecycleSteps();
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
  std::set<uint32_t> frame_ids;
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
        if (owner)
          frame_ids.insert(owner->GetDomNodeId());
        if (!owner || !dependencies.media_ids.contains(owner->GetDomNodeId()))
          continue;
        auto* child = owner ? owner->ContentFrame() : nullptr;
        if (!child || DrawingFrameIdentity(child).empty() ||
            !blink::Canvas2DBitmapProvider::IsReachCommandMode()) {
          result.error = "Embedded plugin or unready frame requires support";
          return result;
        }
        const auto found = children.find(DrawingFrameIdentity(child));
        if (found == children.end() || frame_pictures.size() >= 16 ||
            (child_bytes += found->second.size()) > kMaxPaintBytes) {
          result.error =
              "Missing child frame picture or composition budget exceeded";
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
  size_t media_count = 0;
  std::map<uint32_t, sk_sp<SkData>> media_states;
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
    for (auto& node :
         blink::FlatTreeTraversal::DescendantsOf(*media_document)) {
      auto* media = blink::DynamicTo<blink::HTMLMediaElement>(node);
      if (!media)
        continue;
      // Video state is only replayed where its marker painted. Unpainted
      // videos (e.g. a long feed outside the paint cull rect) are not in this
      // picture, so they neither need transport support nor count against
      // the media budget. Audio has no marker and always does.
      if (media->IsHTMLVideoElement() &&
          !dependencies.media_ids.contains(media->GetDomNodeId())) {
        continue;
      }
      const auto& url = media->currentSrc();
      if (media->HasMediaSource() ||
          blink::HTMLMediaElementEncryptedMedia::mediaKeys(*media) ||
          std::visit([](auto* object) { return object != nullptr; },
                     media->GetSrcObjectVariant()) ||
          (!url.IsEmpty() && !url.ProtocolIs("http") &&
           !url.ProtocolIs("https") && !url.ProtocolIsData())) {
        result.error =
            "MediaSource, MediaStream or encrypted media requires transport "
            "support";
        return result;
      }
      if (url.IsEmpty())
        continue;
      if (web_frame->Parent() || tree_frame != frame.Get()) {
        result.error =
            "Child-frame media requires frame-scoped resource routing";
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
      state.Set(
          "resource",
          url.IsEmpty()
              ? ""
              : base::HexEncode(crypto::hash::Sha256(url.GetString().Utf8())));
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
  }
  if (known) {
    return CaptureIncrementalPicture(frame.Get(), bounds, frame_ids,
                                     frame_pictures, media_states, *known,
                                     sink);
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
  auto* media_canvas = media_recorder.beginRecording(picture->cullRect());
  for (const auto& [id, data] : media_states) {
    media_canvas->drawAnnotation(SkRect::MakeEmpty(), "Reach.MediaState.v1",
                                 data.get());
  }
  picture->playback(media_canvas);
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

}  // namespace

CefDrawingCommands CaptureDrawingCommands(
    blink::WebLocalFrame* web_frame, const std::string& expected_document,
    const std::map<std::string, std::vector<uint8_t>>& children,
    const std::map<std::string, std::vector<uint8_t>>& resources,
    const std::function<void(const std::string&, const void*, size_t)>& sink) {
  return CaptureDrawingCommandsImpl(web_frame, expected_document, children,
                                    resources, sink, nullptr);
}

CefDrawingCommands CaptureIncrementalDrawingCommands(
    blink::WebLocalFrame* web_frame, const std::string& expected_document,
    const std::map<std::string, std::vector<uint8_t>>& children,
    const std::map<std::string, std::vector<uint8_t>>& resources,
    const std::vector<uint8_t>& known,
    const std::function<void(const std::string&, const void*, size_t)>& sink) {
  if (web_frame->Parent()) {
    CefDrawingCommands result;
    result.error = "Incremental capture requires the root frame";
    return result;
  }
  return CaptureDrawingCommandsImpl(web_frame, expected_document, children,
                                    resources, sink, &known);
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
  uint64_t child_pixels = 0;
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
        (child_bytes += found->second.size()) > kMaxPaintBytes) {
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
