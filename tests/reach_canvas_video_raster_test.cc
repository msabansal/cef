#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>

namespace cc {
struct PaintRecord {};
}  // namespace cc
namespace gfx {
struct Rect {};
}  // namespace gfx

struct PaintSerialization {
  bool raster = false;
  std::string error;
};

namespace {
bool safe_record = true;
int visibility_calls = 0;
std::optional<std::set<uint32_t>> visible_markers;
const std::set<uint32_t> kCanvasIds = {42, 43};
}  // namespace

bool CheckPaintRecord(PaintSerialization* state, const cc::PaintRecord&,
                      int depth) {
  assert(state->raster && depth == 0);
  if (!safe_record) {
    state->error = "Paint budget exceeded";
  }
  return safe_record;
}

std::optional<std::set<uint32_t>> VisiblePaintMarkers(
    const cc::PaintRecord&, const gfx::Rect&,
    const std::set<uint32_t>& ids) {
  assert(ids == kCanvasIds);
  ++visibility_calls;
  return visible_markers;
}

#include "reach_canvas_video_raster.inc"

namespace blink {
struct CanvasRenderingContext {
  bool isContextLost() const { return lost; }
  void ReleaseReachCanvasVideo() { ++releases; }
  bool lost = false;
  int releases = 0;
};

struct HTMLCanvasElement {
  struct Dimensions {
    bool IsEmpty() const { return empty; }
    bool empty = false;
  };
  CanvasRenderingContext* RenderingContext() const { return context; }
  bool IsOffscreenCanvasRegistered() const { return worker; }
  Dimensions Size() const { return size; }
  uint32_t GetDomNodeId() {
    ++identity_requests;
    return 42;
  }
  CanvasRenderingContext* context = nullptr;
  Dimensions size;
  bool worker = false;
  int identity_requests = 0;
};

template <typename T>
T* DynamicTo(HTMLCanvasElement* element) {
  return element;
}
}  // namespace blink

struct LayoutObject {
  blink::HTMLCanvasElement* GetNode() const { return element; }
  blink::HTMLCanvasElement* element;
};

struct WebFrame {
  bool Parent() const { return child; }
  bool child = false;
};

std::optional<bool> CollectCanvasIds(
    LayoutObject* object, WebFrame* web_frame, PaintSerialization* dependencies,
    std::set<uint32_t>& canvas_ids) {
  for (; object; object = nullptr) {
#include "reach_canvas_video_ids.inc"
  }
  return true;
}

int main() {
  blink::CanvasRenderingContext context;
  blink::HTMLCanvasElement element;
  element.context = &context;
  LayoutObject object{&element};
  WebFrame frame;
  PaintSerialization dependencies;
  std::set<uint32_t> ids;
  assert(CollectCanvasIds(&object, &frame, &dependencies, ids));
  assert(ids == std::set<uint32_t>{42} && element.identity_requests == 1);
  ids.clear();
  context.lost = true;
  assert(CollectCanvasIds(&object, &frame, &dependencies, ids));
  assert(ids.empty() && context.releases == 1 && element.identity_requests == 1);
  context.lost = false;
  element.size.empty = true;
  assert(CollectCanvasIds(&object, &frame, &dependencies, ids));
  assert(ids.empty() && context.releases == 2 && element.identity_requests == 1);
  element.size.empty = false;
  element.worker = true;
  assert(!CollectCanvasIds(&object, &frame, &dependencies, ids));
  assert(dependencies.error == "Worker OffscreenCanvas video is unsupported");
  element.worker = false;
  frame.child = true;
  assert(!CollectCanvasIds(&object, &frame, &dependencies, ids));
  assert(dependencies.error == "Out-of-process canvas video is unsupported");
  assert(ids.empty() && element.identity_requests == 1);

  const cc::PaintRecord record;
  const gfx::Rect bounds;
  std::string error;

  safe_record = false;
  assert(!CheckRasterCanvasVisibility(record, bounds, kCanvasIds, error));
  assert(error == "Paint budget exceeded" && visibility_calls == 0);

  safe_record = true;
  visible_markers = std::nullopt;
  assert(!CheckRasterCanvasVisibility(record, bounds, kCanvasIds, error));
  assert(error.find("Canvas VIDEO") != std::string::npos);

  visible_markers = std::set<uint32_t>{42};
  assert(!CheckRasterCanvasVisibility(record, bounds, kCanvasIds, error));
  visible_markers = std::set<uint32_t>{42, 43};
  assert(!CheckRasterCanvasVisibility(record, bounds, kCanvasIds, error));

  visible_markers = std::set<uint32_t>{};
  error.clear();
  assert(CheckRasterCanvasVisibility(record, bounds, kCanvasIds, error));
  assert(error.empty());
  std::cout << "actual canvas identity collection and raster boundary: "
               "retirement, bounded proof, fail-closed visible/unknown canvas, "
               "hidden-only HTML fallback PASS\n";
}
