#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace base {
template <typename T>
struct NoDestructor {
  T value;
  T& operator*() { return value; }
};
}  // namespace base

namespace cc {
enum class PaintOpType { kDrawRecord };
struct PaintOp {
  PaintOpType GetType() const { return PaintOpType::kDrawRecord; }
};
struct PaintRecord {
  const std::vector<PaintOp>& buffer() const { return *items; }
  std::shared_ptr<std::vector<PaintOp>> items =
      std::make_shared<std::vector<PaintOp>>();
};
struct DrawRecordOp : PaintOp {
  PaintRecord record;
};
}  // namespace cc

using ChunkKey = std::array<uint8_t, 16>;
using ChunkHash = std::array<uint8_t, 32>;
using SkRect = std::array<float, 4>;
using ImageStates = std::vector<int>;
struct PaintSerialization {
  struct Exported {};
};
constexpr size_t kMaxChunkRefs = 4096;

// NATIVE_CHUNK_CACHE

namespace {
ChunkKey Key(unsigned value) {
  ChunkKey key = {};
  key[0] = static_cast<uint8_t>(value);
  key[1] = static_cast<uint8_t>(value >> 8);
  return key;
}

void AbortedCaptureBytes() {
  ChunkSerializationCache cache;
  for (unsigned index = 0; index < 9; ++index) {
    cache.Store(Key(index),
                {{}, {}, {}, std::vector<uint8_t>(5 * 1024 * 1024), {}, {}});
    size_t retained = 0;
    for (unsigned prior = 0; prior <= index; ++prior) {
      if (const auto* hit = cache.Find(Key(prior), {}, {}, {}))
        retained += hit->bytes.size();
    }
    assert(retained <= 32 * 1024 * 1024);
    assert(cache.Find(Key(index), {}, {}, {}));
  }
}

void AbortedCaptureEntriesAndRetirement() {
  ChunkSerializationCache cache;
  constexpr unsigned count = 9000;
  for (unsigned index = 0; index < count; ++index)
    cache.Store(Key(index), {{}, {}, {}, {1}, {}, {}});
  unsigned retained = 0;
  for (unsigned index = 0; index < count; ++index)
    retained += cache.Find(Key(index), {}, {}, {}) != nullptr;
  assert(retained <= kMaxChunkRefs * 2);
  assert(cache.Find(Key(count - 1), {}, {}, {}));
  for (unsigned capture = 0; capture < 17; ++capture)
    cache.EndCapture();
  assert(!cache.Find(Key(count - 1), {}, {}, {}));
}
}  // namespace

int main() {
  AbortedCaptureBytes();
  AbortedCaptureEntriesAndRetirement();
}
