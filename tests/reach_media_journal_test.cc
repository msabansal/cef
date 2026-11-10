#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#define CORE_EXPORT
template <typename T>
using scoped_refptr = std::shared_ptr<T>;

namespace base {
template <typename T>
class RefCounted {
 public:
  static void Delete(T* value) { delete value; }
};
template <typename T>
auto WrapRefCounted(T* value) {
  return scoped_refptr<T>(value, &RefCounted<T>::Delete);
}
template <typename T>
struct NoDestructor {
  T value;
  T& operator*() { return value; }
};
template <typename T>
class span : public std::span<T> {
 public:
  using std::span<T>::span;
  template <typename Range>
  span(Range& range) : std::span<T>(range.data(), range.size()) {}
  span<T> subspan(size_t offset, size_t count) const {
    assert(offset + count <= this->size());
    return {this->data() + offset, count};
  }
  template <typename U>
  void copy_from(span<U> source) {
    assert(source.size() == this->size());
    std::copy(source.begin(), source.end(), this->begin());
  }
};
template <typename Range>
span(Range&) -> span<typename Range::value_type>;
template <typename T>
auto byte_span_from_ref(const T& value) {
  return span<const uint8_t>(reinterpret_cast<const uint8_t*>(&value),
                             sizeof(value));
}
}  // namespace base

namespace blink {
template <typename T>
class WeakPersistent {
 public:
  explicit WeakPersistent(T* value) : value_(value) {}
  T* Get() const { return value_; }

 private:
  T* value_;
};
class String {
 public:
  String() = default;
  String(const char* value) : value_(value) {}
  bool IsNull() const { return value_.empty(); }
  std::string Utf8() const { return value_; }

 private:
  std::string value_;
};
class LocalFrame {
 public:
  LocalFrame& LocalFrameRoot() { return *this; }
};
struct Document {
  LocalFrame* GetFrame() { return &frame; }
  LocalFrame frame;
};
class HTMLMediaElement {
 public:
  Document& GetDocument() { return document; }
  Document document;
};
}  // namespace blink

// NATIVE_MEDIA_JOURNAL

namespace {
using Journal = blink::ReachMediaJournal;

struct Operation {
  Journal::Kind kind;
  size_t bytes;
  uint8_t first;
};

std::vector<Operation> Operations(const std::vector<uint8_t>& wire) {
  assert(wire.size() >= 25);
  size_t offset = 25;
  std::vector<Operation> result;
  auto read = [&]<typename T>() {
    assert(offset + sizeof(T) <= wire.size());
    T value;
    std::memcpy(&value, wire.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
  };
  while (offset < wire.size()) {
    read.operator()<uint64_t>();
    const auto kind = static_cast<Journal::Kind>(read.operator()<uint8_t>());
    read.operator()<int32_t>();
    read.operator()<double>();
    read.operator()<double>();
    const auto text = read.operator()<uint16_t>();
    offset += text;
    const auto bytes = read.operator()<uint32_t>();
    assert(bytes <= wire.size() - offset);
    result.push_back({kind, bytes, bytes ? wire[offset] : uint8_t{0}});
    offset += bytes;
  }
  return result;
}

void GlobalBudget() {
  std::array<blink::HTMLMediaElement, 13> elements;
  std::vector<scoped_refptr<Journal>> journals;
  std::vector<uint8_t> payload(16 * 1024 * 1024, 9);
  for (auto& element : elements) {
    auto journal = Journal::Start(element);
    const auto buffer = journal->AddBuffer("video/mp4");
    journal->Append(buffer, payload);
    journals.push_back(std::move(journal));
  }
  assert(std::count_if(journals.begin(), journals.end(),
                       [](const auto& journal) { return journal->usable(); }) ==
         11);
  assert((journals.back()->Read(0, 100)[20] & 4) != 0);
  journals.front()->Close();
  auto replacement = Journal::Start(elements.front());
  replacement->Append(replacement->AddBuffer("video/mp4"), payload);
  assert(replacement->usable());
}

void InitializationAfterRepeatedChangeType() {
  blink::HTMLMediaElement element;
  auto journal = Journal::Start(element);
  const auto buffer = journal->AddBuffer("video/mp4");
  journal->Record(Journal::Kind::kChangeType, buffer, 0, 0, "video/mp4");
  journal->Record(Journal::Kind::kChangeType, buffer, 0, 0, "video/mp4");
  std::vector<uint8_t> init = {9};
  std::vector<uint8_t> media = {7};
  journal->Append(buffer, init);
  journal->Append(buffer, media);
  for (int index = 0; index < 65532; ++index)
    journal->Record(Journal::Kind::kTimestampOffset, buffer, index);
  assert(journal->usable());
  const auto operations = Operations(journal->Read(0, 4 * 1024 * 1024));
  assert(operations.size() == 65536);
  const auto append = std::find_if(
      operations.begin(), operations.end(), [](const auto& operation) {
        return operation.kind == Journal::Kind::kAppend;
      });
  assert(append != operations.end() && append->first == 9);
  assert(std::count_if(operations.begin(), operations.end(),
                       [](const auto& operation) {
                         return operation.kind == Journal::Kind::kAppend;
                       }) == 1);
}

void CompactLargeAppend() {
  blink::HTMLMediaElement element;
  auto journal = Journal::Start(element);
  const auto buffer = journal->AddBuffer("video/mp4");
  std::vector<uint8_t> init = {9};
  journal->Append(buffer, init);
  std::vector<uint8_t> small(4096, 7);
  for (int index = 0; index < 15000; ++index)
    journal->Append(buffer, small);
  std::vector<uint8_t> large(16 * 1024 * 1024, 5);
  const auto start = std::chrono::steady_clock::now();
  journal->Append(buffer, large);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  assert(journal->usable());
  const auto wire = journal->Read(0, 64 * 1024 * 1024);
  const auto operations = Operations(wire);
  assert(operations[1].first == 9);
  assert(operations.back().bytes == large.size());
  assert(wire.size() <= 64 * 1024 * 1024);
  assert((journal->Read(3, 100)[20] & 1) != 0);
  std::cout
      << "Large append compaction microseconds: "
      << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
      << std::endl;
}
}  // namespace

int main(int argc, char** argv) {
  Journal::Enable([](blink::LocalFrame&) {});
  const std::string selected = argc > 1 ? argv[1] : "";
  if (selected.empty() || selected == "global")
    GlobalBudget();
  if (selected.empty() || selected == "init")
    InitializationAfterRepeatedChangeType();
  if (selected.empty() || selected == "compact")
    CompactLargeAppend();
}
