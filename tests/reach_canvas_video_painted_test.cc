#include "reach_canvas_video.h"

#include <cassert>
#include <iostream>

int main() {
  const std::vector<std::pair<int, int>> small{{71, 37}, {73, 39}};
  assert(blink::ReachCanvasVideo::Plan(small) == small);
  const auto maps = blink::ReachCanvasVideo::Plan({{2816, 1792}});
  assert(maps && maps->size() == 1);
  assert(maps->front().first < 2816 && maps->front().second < 1792);
  const auto maps_dpr2 = blink::ReachCanvasVideo::Plan({{3584, 2560}});
  assert(maps_dpr2 && maps_dpr2->size() == 1);
  assert(maps_dpr2->front().first > 1792 &&
         maps_dpr2->front().second > 1280);
  blink::ReachCanvasVideo::Inventory maps_inventory;
  assert(maps_inventory.Add(maps_dpr2->front().first,
                            maps_dpr2->front().second));
  assert(maps_inventory.pixels <= 4194304);
  const auto many = blink::ReachCanvasVideo::Plan(
      {{3840, 2160}, {3840, 2160}, {3840, 2160}, {3840, 2160}});
  assert(many && many->size() == 4);
  blink::ReachCanvasVideo::Inventory inventory;
  for (const auto& [width, height] : *many)
    assert(inventory.Add(width, height));
  assert(blink::ReachCanvasVideo::Plan({{4096, 4096}}));
  assert(!blink::ReachCanvasVideo::Plan({{4096, 4097}}));
  assert(!blink::ReachCanvasVideo::Plan({{8193, 1}}));
  assert(!blink::ReachCanvasVideo::Plan({{1, 8193}}));
  assert(!blink::ReachCanvasVideo::Plan({{0, 1}}));
  const std::vector<std::pair<int, int>> weather(5, {32, 28});
  assert(blink::ReachCanvasVideo::Plan(weather) == weather);
  const std::vector<std::pair<int, int>> tiny(79, {8, 8});
  assert(blink::ReachCanvasVideo::Plan(tiny) == tiny);
  assert(!blink::ReachCanvasVideo::Plan(
      std::vector<std::pair<int, int>>(80, {1, 1})));
  const auto thin = blink::ReachCanvasVideo::Plan(
      std::vector<std::pair<int, int>>(79, {3840, 1}));
  assert(thin && thin->size() == 79 && thin->front().first < 3840);
  blink::ReachCanvasVideo::Inventory thin_inventory;
  for (const auto& [width, height] : *thin)
    assert(thin_inventory.Add(width, height));
  blink::ReachCanvasVideo::Inventory codec_boundary;
  for (unsigned stream = 0; stream < 32; ++stream)
    assert(codec_boundary.Add(1024, 96));
  assert(codec_boundary.codec == 320 * 1024 * 1024);
  assert(!codec_boundary.Add(1, 1));
  blink::ReachCanvasVideo::Inventory codec_overflow;
  for (unsigned stream = 0; stream < 29; ++stream)
    assert(codec_overflow.Add(1024, 97));
  assert(!codec_overflow.Add(1024, 97));

  auto budget = std::make_shared<blink::ReachCanvasVideo::Budget>();
  blink::ReachCanvasVideo visible;
  visible.SetBudget(budget);
  unsigned reads = 0;
  auto read = [&] {
    ++reads;
    return std::vector<uint8_t>(2048 * 2048 * 4, 17);
  };
  assert(visible.Checkpoint(2048, 2048, read));
  const auto stream = visible.stream();
  const auto generation = visible.generation();
  const auto sequence = visible.sequence();
  assert(visible.IsCurrent());

  visible.ReleaseAdmission();
  assert(budget->active.empty());
  assert(visible.IsCurrent() && visible.rgba().front() == 17);
  assert(visible.Checkpoint(2048, 2048, read));
  assert(reads == 1 && visible.stream() == stream);
  assert(visible.generation() == generation && visible.sequence() == sequence);

  visible.ReleaseAdmission();
  blink::ReachCanvasVideo replacement;
  replacement.SetBudget(budget);
  assert(replacement.Checkpoint(
      1920, 1080, [] { return std::vector<uint8_t>(1920 * 1080 * 4, 81); }));
  assert(budget->active.size() == 1);
  assert(visible.IsCurrent() && visible.rgba().front() == 17);
  visible.Dirty();
  assert(!visible.IsCurrent());
  assert(replacement.IsCurrent());
  replacement.Epoch();
  assert(!replacement.IsCurrent() && budget->active.empty());
  std::cout << "painted admission preserves cached identities and retires stale budgets\n";
}
