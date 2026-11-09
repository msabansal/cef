#include "reach_canvas_video.h"

#include <cassert>
#include <iostream>

int main() {
  const std::vector<std::pair<int, int>> small{{71, 37}, {73, 39}};
  assert(blink::ReachCanvasVideo::Plan(small) == small);
  const auto maps = blink::ReachCanvasVideo::Plan({{2816, 1792}});
  assert(maps && maps->size() == 1);
  assert(maps->front().first < 2816 && maps->front().second < 1792);
  const auto many = blink::ReachCanvasVideo::Plan(
      {{3840, 2160}, {3840, 2160}, {3840, 2160}, {3840, 2160}});
  assert(many && many->size() == 4);
  blink::ReachCanvasVideo::Inventory inventory;
  for (const auto& [width, height] : *many)
    assert(inventory.Add(width, height));
  assert(!blink::ReachCanvasVideo::Plan({{4096, 4096}}));
  assert(!blink::ReachCanvasVideo::Plan(
      {{1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}}));

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
