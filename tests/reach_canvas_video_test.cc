#include "reach_canvas_video.h"

#include <cassert>
#include <iostream>

int main() {
  blink::ReachCanvasVideo frame;
  unsigned reads = 0;
  std::vector<uint8_t> buffer(3 * 3 * 4, 17);
  auto read = [&] {
    ++reads;
    return buffer;
  };
  assert(frame.Checkpoint(3, 3, read));
  const auto stream = frame.stream();
  const auto sequence = frame.sequence();
  const auto generation = frame.generation();
  buffer.assign(buffer.size(), 0);  // Compositor discarded the drawing buffer.
  assert(frame.Checkpoint(3, 3, read));
  assert(reads == 1 && frame.rgba().front() == 17);
  assert(frame.sequence() == sequence);
  buffer = frame.rgba();
  buffer[3] = 4;  // Alpha-only drawing is new content, not an RGB hash match.
  frame.Dirty();
  assert(frame.Checkpoint(3, 3, read));
  assert(reads == 2 && frame.rgba()[3] == 4);
  assert(frame.sequence() == sequence + 1);
  frame.Epoch();
  assert(frame.Checkpoint(2, 2, [] { return std::vector<uint8_t>(16); }));
  assert(frame.generation() > generation && frame.stream() == stream);
  frame.Dirty();
  assert(!frame.Checkpoint(2, 2, [] { return std::vector<uint8_t>(15); }));
  assert(!frame.error().empty());
  blink::ReachCanvasVideo replacement;
  assert(replacement.stream() != stream);
  assert(!replacement.Checkpoint(3841, 1, read));
  assert(!replacement.Checkpoint(0, 1, read));
  blink::ReachCanvasVideo::Inventory inventory;
  assert(inventory.Add(3840, 1080));
  assert(!inventory.Add(400, 400));  // Even-padded aggregate exceeds 4M.
  blink::ReachCanvasVideo::Inventory odd;
  assert(odd.Add(1, 1) && odd.Add(1, 1) && odd.Add(1, 1) && odd.Add(1, 1));
  assert(!odd.Add(1, 1));
  blink::ReachCanvasVideo::Inventory full;
  assert(full.Add(2047, 2047));
  assert(!full.Add(1, 1));  // Admission uses even padding, not logical pixels.
  auto budget = std::make_shared<blink::ReachCanvasVideo::Budget>();
  blink::ReachCanvasVideo resized;
  resized.SetBudget(budget);
  assert(resized.Checkpoint(1, 1, [] { return std::vector<uint8_t>(4); }));
  assert(resized.Checkpoint(
      2047, 2047, [] { return std::vector<uint8_t>(2047 * 2047 * 4); }));
  blink::ReachCanvasVideo sibling;
  sibling.SetBudget(budget);
  unsigned rejected_reads = 0;
  auto rejected_read = [&] {
    ++rejected_reads;
    return std::vector<uint8_t>(4);
  };
  assert(!sibling.Checkpoint(1, 1, rejected_read));
  assert(rejected_reads == 0);
  resized.Epoch();  // Loss/removal releases admission without waiting for GC.
  blink::ReachCanvasVideo after_loss;
  after_loss.SetBudget(budget);
  assert(after_loss.Checkpoint(1, 1, rejected_read));
  assert(rejected_reads == 1);
  std::cout << "canvas snapshot ownership, alpha, epochs and bounds passed\n";
}
