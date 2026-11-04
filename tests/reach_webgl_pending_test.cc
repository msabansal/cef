#include "reach_webgl_pending.h"

#include <cassert>
#include <iostream>

int main() {
  std::atomic_size_t bytes{0};
  {
    blink::ReachWebGLPending queue(bytes);
    std::string commands;
    uint64_t end = 0;
    for (uint64_t position = 0; position < 150000; ++position) {
      assert(queue.Append("{}").empty());
      const size_t before_read = bytes.load();
      assert(queue.Read(0, position, 1024, &commands, &end).empty());
      assert(end == position + 1 && commands == "[{}]");
      assert(bytes.load() == before_read);  // Reading is not retirement.
      assert(queue.Read(0, position, 1024, &commands, &end).empty());
      assert(!queue.Applied(0, end).empty());  // Read is not a sent prefix.
      assert(!queue.Applied(1, end).empty());  // Stale generation.
      assert(bytes.load() == before_read);
      assert(!queue.Sent(1, end).empty());
      assert(!queue.Sent(0, end + 1).empty());
      assert(queue.Sent(0, end).empty());
      assert(queue.Sent(0, end).empty());
      assert(bytes.load() == before_read);  // Sending is not application.
      assert(queue.Applied(0, end).empty());
      assert(queue.Applied(0, end).empty());  // Duplicate cumulative ack.
      assert(queue.Applied(0, 0).empty());
      assert(bytes.load() == 0);
    }
    assert(queue.Append("{}").empty());
    assert(!queue.Applied(0, 150001).empty());  // Exists, but was never read.
    assert(!queue.Applied(0, 150002).empty());
    assert(!queue.Read(0, 150001, 1024, &commands, &end).empty());
    assert(!queue.Read(0, 149999, 1024, &commands, &end).empty());
    assert(!queue.Read(0, 150000, 3, &commands, &end).empty());
    assert(!queue.Read(1, 150000, 1024, &commands, &end).empty());
    assert(!queue.Applied(0, 150001).empty());  // Failed read changed nothing.
    assert(queue.Read(0, 150000, 1024, &commands, &end).empty());
    assert(queue.Sent(0, end).empty());
    assert(queue.Applied(0, end).empty());
    assert(!queue.Applied(1, end).empty());  // Even a duplicate needs identity.
  }
  assert(bytes.load() == 0);
  {
    blink::ReachWebGLPending first(bytes), second(bytes), third(bytes);
    const std::string payload(1024 * 1024, 'x');
    for (int i = 0; i < 85; ++i) {
      assert(first.Append(payload).empty());
      assert(second.Append(payload).empty());
      assert(third.Append(payload).empty());
    }
    assert(bytes.load() <= blink::ReachWebGLPending::kLimit);
    const size_t before_failure = bytes.load();
    assert(!third.Append(payload).empty());
    assert(bytes.load() == before_failure);
    std::string commands;
    uint64_t end = 0;
    assert(first.Read(0, 0, 2 * 1024 * 1024, &commands, &end).empty());
    assert(first.Sent(0, end).empty());
    assert(first.Applied(0, end).empty());
    assert(third.Append(payload).empty());
  }
  assert(bytes.load() == 0);  // Context destruction releases pending storage.
  {
    blink::ReachWebGLPending queue(bytes, 9);
    std::string commands;
    uint64_t end = 0;
    assert(queue.Append("{}", 4096).empty());
    assert(!queue.Read(9, 0, 1024, &commands, &end).empty());
    assert(!queue.Applied(9, 1).empty());
    assert(queue.Read(9, 0, 8192, &commands, &end).empty());
    assert(queue.Sent(9, 1).empty());
    assert(queue.Applied(9, 1).empty());
  }
  std::cout << "WebGL pending FIFO: lifetime, reads, applied prefixes, aggregate PASS\n";
}
