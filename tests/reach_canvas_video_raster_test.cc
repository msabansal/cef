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

int main() {
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
  std::cout << "actual raster boundary: bounded proof, fail-closed visible/"
               "unknown canvas, hidden-only HTML fallback PASS\n";
}
