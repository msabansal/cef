#include "reach_webgl_pending.h"

#include <cassert>
#include <iostream>

// Chromium 9ce0cc67: String::size_type = wtf_size_t = uint32_t.
struct BlinkStringIndexFixture {
  using size_type = uint32_t;
  size_type units;
  const char16_t* data;

  size_type length() const { return units; }
  char16_t operator[](size_type index) const {
    assert(index < units);
    return data[index];
  }
};

template <typename Pending>
void CheckLargeRead(std::atomic_size_t& bytes) {
  Pending queue(bytes, 9);
  const std::string payload(20 * 1024 * 1024, 'x');
  if constexpr (!requires { typename Pending::Slice; }) {
    assert(queue.Append(payload).empty());
    std::string result;
    uint64_t end = 0;
    assert(queue.Read(9, 0, 8 * 1024 * 1024, &result, &end).empty());
  } else {
    const size_t original_bytes = 12 * 1024 * 1024;
    const std::string hash(64, 'a');
    assert(queue.Append(payload, original_bytes, hash).empty());
    typename Pending::Slice slice;
    std::string chunk;
    assert(!queue.ReadSlice(8, 0, 0, 98304, false, &chunk, &slice).empty());
    assert(!queue.ReadSlice(9, 1, 0, 98304, false, &chunk, &slice).empty());
    assert(!queue.ReadSlice(9, 0, 1, 98304, false, &chunk, &slice).empty());
    assert(!queue.ReadSlice(9, 0, 0, 98305, false, &chunk, &slice).empty());
    std::string result;
    size_t offset = 0;
    while (offset < payload.size()) {
      assert(
          queue.ReadSlice(9, 0, offset, 98304, false, &chunk, &slice).empty());
      assert(slice.total == payload.size() && slice.length == chunk.size());
      assert(slice.original_hash == hash &&
             slice.original_bytes == original_bytes);
      assert(slice.length <= 98304 && slice.length > 0);
      const auto first = chunk;
      assert(
          queue.ReadSlice(9, 0, offset, 98304, false, &chunk, &slice).empty());
      assert(chunk == first);
      assert(!queue.Sent(9, 1).empty());
      assert(!queue.Applied(9, 1).empty());
      result += chunk;
      offset += chunk.size();
    }
    assert(result == payload);
    assert(!queue.Sent(9, 1).empty());  // The original is still unread.
    assert(!queue.ReadSlice(9, 0, 1, 98304, true, &chunk, &slice).empty());
    offset = 0;
    while (offset < original_bytes) {
      assert(
          queue.ReadSlice(9, 0, offset, 98304, true, &chunk, &slice).empty());
      assert(chunk.empty() && slice.total == original_bytes &&
             slice.length > 0);
      offset += slice.length;
      if (offset < original_bytes) assert(!queue.Sent(9, 1).empty());
    }
    assert(queue.Sent(9, 1).empty());
    assert(queue.Applied(9, 1).empty());
    assert(!queue.ReadSlice(9, 0, 0, 98304, false, &chunk, &slice).empty());
    assert(queue.ReadSlice(9, 1, 0, 98304, false, &chunk, &slice).empty());
    assert(slice.total == 0 && slice.length == 0 && chunk.empty());
  }
}

template <typename Pending>
void CheckAccounting(std::atomic_size_t& bytes) {
  if constexpr (requires { typename Pending::Reservation; }) {
    Pending queue(bytes);
    typename Pending::Reservation held;
    assert(held.Reserve(bytes, Pending::kLimit - 16));
    assert(!held.Grow(bytes, 17));
    assert(!queue.Append("{}").empty());
    typename Pending::Reservation moved = std::move(held);
    assert(queue.Append("{}", 0, {}, &moved).empty());
    assert(bytes.load() < 1024);
    assert(Pending::JSONStringBound(std::u16string(2 * 1024 * 1024, u'x')) ==
           2 * 1024 * 1024 + 2);
    assert(Pending::JSONStringBound(std::u16string(u"\n\"<\U0001f600")) == 16);
    assert(Pending::JSONStringBound(BlinkStringIndexFixture{0, u""}) == 2);
    assert(Pending::JSONStringBound(BlinkStringIndexFixture{3, u"abc"}) == 5);
    assert(Pending::JSONStringBound(
               BlinkStringIndexFixture{5, u"\n\"<\U0001f600"}) == 16);
    assert(Pending::JSONStringBound(
               BlinkStringIndexFixture{1, u"\xd800"}) == 8);
    assert(Pending::JSONStringBound(
               BlinkStringIndexFixture{1, u"\xdc00"}) == 8);
    assert(Pending::JSONStringBound(
               BlinkStringIndexFixture{3, u"\U0001f600X"}) == 7);
    std::string command;
    uint64_t end = 0;
    assert(queue.Append(std::string(200000, 'x')).empty());
    assert(queue.Read(0, 0, 1024, &command, &end).empty() && end == 1);
    typename Pending::Slice slice;
    assert(queue.ReadSlice(0, 1, 0, 98304, false, &command, &slice).empty());
    assert(queue.ReadSlice(0, 0, 0, 98304, false, &command, &slice).empty());
    assert(
        queue.ReadSlice(0, 1, 98304, 98304, false, &command, &slice).empty());
    assert(!queue.Sent(0, 2).empty());
  }
}

int main() {
  std::atomic_size_t bytes{0};
  CheckLargeRead<blink::ReachWebGLPending>(bytes);
  assert(bytes.load() == 0);
  CheckAccounting<blink::ReachWebGLPending>(bytes);
  assert(bytes.load() == 0);
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
  std::cout << "WebGL pending FIFO: lifetime, reads, applied prefixes, "
               "aggregate PASS\n";
}
