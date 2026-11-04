#include <cassert>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "reach_image_bitmap_original.h"
#include "reach_webgl_pending.h"

#define DCHECK(value) assert(value)
#define UNSAFE_BUFFERS(value) value
using blink::ReachImageBitmapOriginal;
using blink::ReachWebGLPending;
template <typename T>
using sk_sp = std::shared_ptr<T>;

// Only Chromium/Skia boundaries are substituted. Upload routing, original
// capture, replay-state wrapping and retirement below are production methods.
std::atomic_size_t& RetainedBytes();
namespace base {
template <typename T>
using span = std::span<T>;
std::string NumberToString(uint64_t value) { return std::to_string(value); }
struct DictValue;
struct Value;
struct ListValue {
  std::vector<int> values;
  std::vector<DictValue> references;
  void Append(int value) { values.push_back(value); }
  void Append(DictValue);
  void Append(Value);
};
struct DictValue {
  std::map<std::string, std::string> strings;
  std::map<std::string, double> numbers;
  std::map<std::string, ListValue> lists;
  std::map<std::string, bool> booleans;
  std::set<std::string> doubles;
  void Set(std::string key, const char* value) {
    Set(std::move(key), std::string(value));
  }
  void Set(std::string key, bool value) {
    booleans.emplace(std::move(key), value);
  }
  void Set(std::string key, std::string value) {
    strings.emplace(std::move(key), std::move(value));
  }
  void Set(std::string key, std::string_view value) {
    Set(std::move(key), std::string(value));
  }
  template <typename T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
  void Set(std::string key, T value) {
    if constexpr (std::is_floating_point_v<T>) doubles.insert(key);
    numbers.emplace(std::move(key), static_cast<double>(value));
  }
  void Set(std::string key, ListValue value) {
    lists.emplace(std::move(key), std::move(value));
  }
  const std::string* FindString(const std::string& key) const {
    auto it = strings.find(key);
    return it == strings.end() ? nullptr : &it->second;
  }
  size_t EstimateMemoryUsage() const;
};
struct Value {
  std::optional<DictValue> dictionary;
  Value() = default;
  explicit Value(DictValue value) : dictionary(std::move(value)) {}
};
void ListValue::Append(Value value) {
  assert(value.dictionary);
  Append(std::move(*value.dictionary));
}
std::string Base64Encode(span<const uint8_t> bytes) {
  return std::string(((bytes.size() + 2) / 3) * 4, 'A');
}
void ListValue::Append(DictValue value) {
  references.push_back(std::move(value));
}
size_t DictValue::EstimateMemoryUsage() const {
  size_t bytes = 0;
  for (const auto& [key, value] : strings)
    bytes += key.capacity() + value.capacity() + sizeof(key) + sizeof(value) +
             4 * sizeof(void*);
  for (const auto& [key, value] : lists) {
    bytes += key.capacity() + sizeof(key) + sizeof(value) + 4 * sizeof(void*) +
             value.values.capacity() * sizeof(int) +
             value.references.capacity() * sizeof(DictValue);
    for (const auto& reference : value.references)
      bytes += reference.EstimateMemoryUsage();
  }
  return bytes;
}
std::optional<std::string> WriteJson(const DictValue& value) {
  if (!value.lists.contains("args")) {
    std::string json = "{";
    for (const auto& [key, text] : value.strings)
      json += (json.size() == 1 ? "" : ",") + std::string("\"") + key +
              "\":\"" + text + "\"";
    for (const auto& [key, number] : value.numbers)
      json += ",\"" + key +
              "\":" + std::to_string(static_cast<uint64_t>(number)) +
              (value.doubles.contains(key) ? ".0" : "");
    for (const auto& [key, boolean] : value.booleans)
      json += ",\"" + key + "\":" + (boolean ? "true" : "false");
    return json + "}";
  }
  const auto& args = value.lists.at("args");
  std::string json = "{\"op\":\"" + value.strings.at("op") + "\",\"args\":[";
  for (int number : args.values)
    json += (json.back() == '[' ? "" : ",") + std::to_string(number);
  for (const auto& reference : args.references) {
    if (reference.strings.contains("image")) {
      json += ",{\"image\":\"" + reference.strings.at("image") + "\"}";
    } else {
      json += ",{\"type\":\"" + reference.strings.at("type") +
              "\",\"data\":\"" + reference.strings.at("data") + "\"}";
    }
  }
  return json + "]}";
}
size_t maximum_serialized_ownership = 0;
struct JSONWriter {
  static bool Write(const DictValue& value, std::string* output) {
    const size_t simultaneous =
        value.EstimateMemoryUsage() + output->capacity();
    assert(RetainedBytes().load() >= simultaneous &&
           "captured input and JSON must both be charged during serialization");
    assert(RetainedBytes().load() <= ReachWebGLPending::kLimit);
    maximum_serialized_ownership =
        std::max(maximum_serialized_ownership, simultaneous);
    if (!value.lists.contains("args")) {
      const auto json = WriteJson(value);
      if (!json) return false;
      output->assign(*json);
      return true;
    }
    output->assign("{\"op\":\"");
    output->append(value.strings.at("op"));
    output->append("\",\"args\":[");
    bool first = true;
    auto separator = [&]() {
      if (!first) output->push_back(',');
      first = false;
    };
    const auto& args = value.lists.at("args");
    for (int number : args.values) {
      separator();
      output->append(std::to_string(number));
    }
    for (const auto& reference : args.references) {
      separator();
      if (reference.strings.contains("image")) {
        output->append("{\"image\":\"");
        output->append(reference.strings.at("image"));
      } else {
        output->append("{\"type\":\"");
        output->append(reference.strings.at("type"));
        output->append("\",\"data\":\"");
        output->append(reference.strings.at("data"));
      }
      output->append("\"}");
    }
    output->append("]}");
    return true;
  }
};
}  // namespace base

class SkData {
 public:
  using Release = void (*)(const void*, void*);
  static sk_sp<SkData> MakeWithProc(const void* data, size_t size,
                                    Release release, void* context) {
    return std::shared_ptr<SkData>(new SkData(data, size, release, context));
  }
  ~SkData() { release_(data_, context_); }
  const void* data() const { return data_; }
  size_t size() const { return size_; }
  bool isEmpty() const { return size_ == 0; }

 private:
  SkData(const void* data, size_t size, Release release, void* context)
      : data_(data), size_(size), release_(release), context_(context) {}
  const void* data_;
  size_t size_;
  Release release_;
  void* context_;
};

std::atomic_size_t aggregate{0};
std::atomic_size_t& RetainedBytes() { return aggregate; }
std::string Hash(base::span<const uint8_t> bytes) {
  assert(bytes.size() >= 4 &&
         (std::vector<uint8_t>(bytes.begin(), bytes.begin() + 4) ==
          std::vector<uint8_t>{137, 80, 78, 71}));
  return std::string(64, 'a');
}
auto original_hasher = &Hash;
constexpr size_t kMaxCommandBytes = ReachWebGLPending::kLimit;
struct ReachWebGLCommand {
  base::DictValue value;
  std::string error;
  size_t input_bytes = 0;
  ReachWebGLPending::Reservation reservation;
  ReachWebGLPending::Reservation original_reservation;
  bool texture_source = false;
  std::string original_hash;
  sk_sp<const SkData> original;
  bool original_precharged = false;
  std::optional<ReachImageBitmapOriginal::Unpack> unpack_before, unpack_after;
};
class ReachWebGLJournal {
 public:
  struct Original {
    sk_sp<const SkData> data;
    uint64_t last_next;
    size_t charge;
  };
  static constexpr size_t kMaxOriginalBytes =
      ReachImageBitmapOriginal::kMaxBytes;
  ReachWebGLCommand captured_command;
  ReachWebGLCommand* pending_ = &captured_command;
  ReachWebGLPending commands_{aggregate, 5};
  std::map<std::string, Original> originals_;
  size_t original_bytes_ = 0;
  uint64_t generation_ = 5;
  uint64_t id_ = 7;
  std::string error_;
  ReachWebGLJournal() { Reset("texImage2D"); }
  ~ReachWebGLJournal() { RetainedBytes().fetch_sub(original_bytes_); }
  void Reset(std::string op) {
    captured_command = {};
    captured_command.texture_source = true;
    captured_command.error =
        op + ": WebGL source argument requires command transport";
    captured_command.value.Set("op", std::move(op));
  }
  bool usable() const { return error_.empty(); }
  void Fail(std::string_view error) { error_ = error; }
  void NotifyChange() {}
  bool HasTextureSource() const { return pending_->texture_source; }
  void RejectTextureUpload(std::string_view);
  void OriginalTexture(base::ListValue, sk_sp<const SkData>, int, int);
  bool ReserveOriginalBytes(size_t);
  void OriginalImageBitmapTexture(
      base::ListValue, std::shared_ptr<const ReachImageBitmapOriginal>, int,
      int, ReachImageBitmapOriginal::Unpack);
  void Append(ReachWebGLCommand);
  std::string Applied(uint64_t, uint64_t);
  base::Value Bytes(ReachWebGLCommand&, std::string_view,
                    base::span<const uint8_t>);
  std::string ReadSlice(uint64_t, size_t, size_t, bool, std::vector<uint8_t>*,
                        std::map<std::string, std::vector<uint8_t>>*);
};

// NATIVE_JOURNAL_METHODS

enum class PredefinedColorSpace { kSRGB, kDisplayP3 };
constexpr int kTexImage2D = 1;
constexpr int kTexSubImage2D = 2;
constexpr int kPremul_SkAlphaType = 1;
struct ExceptionState {};
struct StaticBitmapImage {
  std::shared_ptr<const ReachImageBitmapOriginal> original;
  const auto& ReachOriginal() const { return original; }
  std::string ReachOriginalError() const { return "missing source provenance"; }
  int GetAlphaType() const { return kPremul_SkAlphaType; }
};
struct ImageBitmap {
  std::shared_ptr<StaticBitmapImage> image =
      std::make_shared<StaticBitmapImage>();
  auto BitmapImage() const { return image; }
  int width() const { return 2; }
  int height() const { return 2; }
};
struct TexImageParams {
  int function_id = kTexImage2D;
  int target = 3553;
  int level = 0;
  int internalformat = 6408;
  int format = 6408;
  int type = 5121;
  int xoffset = 1;
  int yoffset = 2;
  std::optional<int> width, height, depth;
  bool unpack_premultiply_alpha = false;
  bool unpack_flip_y = false;
  int unpack_skip_pixels = 0;
  int unpack_skip_rows = 0;
  int unpack_skip_images = 0;
  int unpack_image_height = 0;
};
class WebGLRenderingContextBase {
 public:
  ReachWebGLJournal capture;
  ReachWebGLJournal* reach_journal_ = &capture;
  int unpack_flip_y_ = 1;
  int unpack_premultiply_alpha_ = 0;
  int unpack_colorspace_conversion_ = 0;
  int unpack_row_length_ = 0;
  PredefinedColorSpace unpack_color_space_ = PredefinedColorSpace::kSRGB;
  int captured_rgba = 0;
  int server_uploads = 0;
  bool valid = true;
  bool isContextLost() const { return false; }
  const char* GetTexImageFunctionName(int) const { return "texImage2D"; }
  bool ValidateImageBitmap(const char*, ImageBitmap*, ExceptionState&) {
    return valid;
  }
  bool ValidateTexImageBinding(const TexImageParams&) const { return true; }
  bool ValidateTexImageSubRectangle(const TexImageParams&, ImageBitmap*,
                                    bool*) {
    return true;
  }
  bool ValidateTexFunc(const TexImageParams&, std::optional<int>,
                       std::optional<int>) {
    return true;
  }
  void TexImageStaticBitmapImage(const TexImageParams&, StaticBitmapImage*,
                                 bool) {
    ++server_uploads;
    const auto& command = capture.captured_command;
    if (reach_journal_ && command.texture_source &&
        command.error ==
            command.value.strings.at("op") +
                ": WebGL source argument requires command transport") {
      ++captured_rgba;
    }
  }
  void TexImageHelperImageBitmap(TexImageParams, ImageBitmap*, ExceptionState&);
};

// NATIVE_UPLOAD_METHOD

int main() {
  {
    aggregate.store(ReachImageBitmapOriginal::kLimit - 100);
    bool copied = false;
    auto source = ReachImageBitmapOriginal::CreateCopied(
        aggregate, 4, {0, 0, 0}, [&copied] {
          copied = true;
          return std::vector<uint8_t>{137, 80, 78, 71};
        });
    assert(!source && !copied);
    aggregate.store(0);
    source =
        ReachImageBitmapOriginal::CreateCopied(aggregate, 4, {0, 0, 0}, [] {
          assert(aggregate.load() >= 260);
          return std::vector<uint8_t>{137, 80, 78, 71};
        });
    assert(source);
  }
  assert(aggregate.load() == 0);
  {
    ReachWebGLJournal producer;
    producer.Reset("bufferData");
    producer.captured_command.error.clear();
    producer.captured_command.texture_source = false;
    const std::vector<uint8_t> input(120 * 1024 * 1024);
    base::ListValue args;
    args.Append(producer.Bytes(producer.captured_command, "Uint8Array", input));
    assert(producer.captured_command.error.empty());
    producer.captured_command.value.Set("args", std::move(args));
    producer.Append(std::move(producer.captured_command));
    assert(!producer.usable());
    assert(producer.error_ ==
           "Combined WebGL pending serialization budget exceeded");
    assert(producer.commands_.next() == 0);
    assert(base::maximum_serialized_ownership == 0);
    assert(aggregate.load() == 0);
  }
  assert(aggregate.load() == 0);
  for (const bool sibling_pending : {false, true}) {
    ReachWebGLPending sibling(aggregate, 17);
    if (sibling_pending)
      assert(sibling.Append(std::string(20 * 1024 * 1024, 's')).empty());
    const size_t sibling_charge = aggregate.load();
    ReachWebGLJournal producer;
    producer.Reset("bufferData");
    producer.captured_command.error.clear();
    producer.captured_command.texture_source = false;
    const std::vector<uint8_t> input(90 * 1024 * 1024);
    base::ListValue args;
    args.Append(producer.Bytes(producer.captured_command, "Uint8Array", input));
    assert(producer.captured_command.error.empty());
    producer.captured_command.value.Set("args", std::move(args));
    producer.Append(std::move(producer.captured_command));
    if (sibling_pending) {
      assert(!producer.usable() && producer.commands_.next() == 0);
      assert(producer.error_ ==
             "Combined WebGL pending serialization budget exceeded");
      assert(aggregate.load() == sibling_charge);
    } else {
      assert(producer.usable() && producer.commands_.next() == 1);
      assert(aggregate.load() < 121 * 1024 * 1024);
    }
    assert(base::maximum_serialized_ownership >= 240 * 1024 * 1024);
    assert(base::maximum_serialized_ownership <= ReachWebGLPending::kLimit);
  }
  assert(aggregate.load() == 0);
  {
    ReachWebGLJournal producer;
    producer.Reset("bufferData");
    producer.captured_command.error.clear();
    producer.captured_command.texture_source = false;
    std::vector<uint8_t> input(9 * 1024 * 1024);
    auto encoded =
        producer.Bytes(producer.captured_command, "Uint8Array", input);
    assert(producer.captured_command.error.empty());
    assert(aggregate.load() >= 12 * 1024 * 1024);
    base::ListValue args;
    args.Append(34962);
    args.Append(std::move(encoded));
    producer.captured_command.value.Set("args", std::move(args));
    producer.Append(std::move(producer.captured_command));
    assert(producer.usable());
    std::string legacy;
    uint64_t end = 0;
    assert(
        !producer.commands_.Read(5, 0, 8 * 1024 * 1024, &legacy, &end).empty());
    std::vector<uint8_t> header;
    std::map<std::string, std::vector<uint8_t>> data;
    size_t offset = 0;
    std::string result;
    while (!producer.commands_.Sent(5, 1).empty()) {
      data.clear();
      assert(
          producer.ReadSlice(0, offset, 98304, false, &header, &data).empty());
      const auto& chunk = data.at("chunk");
      assert(chunk.size() <= 98304 && !chunk.empty());
      result.append(chunk.begin(), chunk.end());
      offset += chunk.size();
      assert(!producer.Applied(5, 1).empty());
    }
    assert(result.find(std::string(12 * 1024 * 1024, 'A')) !=
           std::string::npos);
    assert(producer.Applied(5, 1).empty());
    assert(aggregate.load() == 0);
  }
  assert(aggregate.load() == 0);
  {
    ReachWebGLJournal journal;
    aggregate.store(ReachWebGLPending::kLimit - 100);
    assert(!journal.ReserveOriginalBytes(4));
    aggregate.store(0);
    journal.Reset("texImage2D");
    assert(journal.ReserveOriginalBytes(4));
    assert(aggregate.load() == 260);  // Charged before copying HTML bytes.
    const std::vector<uint8_t> bytes{137, 80, 78, 71};
    auto encoded = SkData::MakeWithProc(
        bytes.data(), bytes.size(), [](const void*, void*) {}, nullptr);
    journal.OriginalTexture({}, std::move(encoded), 2, 2);
    journal.Append(std::move(journal.captured_command));
    assert(journal.usable() && journal.original_bytes_ == 260);
    std::string read;
    uint64_t end = 0;
    assert(journal.commands_.Read(5, 0, 4096, &read, &end).empty());
    assert(journal.commands_.Sent(5, end).empty());
    assert(journal.Applied(5, end).empty());
    assert(aggregate.load() == 0);
  }
  assert(aggregate.load() == 0);
  {
    ReachWebGLJournal journal;
    std::vector<uint8_t> encoded(12 * 1024 * 1024);
    std::copy_n(std::vector<uint8_t>{137, 80, 78, 71}.begin(), 4,
                encoded.begin());
    auto source =
        ReachImageBitmapOriginal::Create(aggregate, encoded, {0, 0, 0});
    assert(source);
    journal.OriginalImageBitmapTexture({}, source, 2, 2, {0, 0, 0});
    assert(!journal.captured_command.value.lists.at("args")
                .references[0]
                .doubles.contains("length"));
    journal.Append(std::move(journal.captured_command));
    assert(journal.usable());
    // The pending SkData shares the precharged immutable encoded allocation.
    assert(aggregate.load() < encoded.size() + 8192);
    std::string prefix;
    uint64_t end = 0;
    assert(journal.commands_.Read(5, 0, 4096, &prefix, &end).empty());
    assert(end == 3);
    std::vector<uint8_t> header;
    std::map<std::string, std::vector<uint8_t>> data;
    assert(!journal.ReadSlice(0, 0, 98304, true, &header, &data).empty());
    assert(!journal.ReadSlice(4, 0, 98304, false, &header, &data).empty());
    assert(journal.ReadSlice(3, 0, 98304, false, &header, &data).empty());
    assert(std::string(header.begin(), header.end()).find(".0") ==
           std::string::npos);
    assert(std::string(header.begin(), header.end())
               .find("\"image\":\"" + std::string(64, 'a') + "\"") !=
           std::string::npos);
    assert(!journal.commands_.Sent(5, 4).empty());
    size_t offset = 0;
    while (offset < encoded.size()) {
      data.clear();
      assert(journal.ReadSlice(3, offset, 98304, true, &header, &data).empty());
      const auto& chunk = data.at("chunk");
      assert(chunk.size() <= 98304 && !chunk.empty());
      assert(std::equal(chunk.begin(), chunk.end(), encoded.begin() + offset));
      offset += chunk.size();
      if (offset < encoded.size())
        assert(!journal.commands_.Sent(5, 4).empty());
      assert(!journal.Applied(5, 4).empty());
    }
    assert(journal.commands_.Sent(5, 4).empty());
    assert(journal.Applied(5, 4).empty());
    assert(journal.originals_.empty());
    assert(!journal.ReadSlice(3, 0, 98304, true, &header, &data).empty());
  }
  assert(aggregate.load() == 0);
  ExceptionState exception;
  for (const int function : {kTexImage2D, kTexSubImage2D}) {
    for (int bits = 0; bits < 64; ++bits) {
      WebGLRenderingContextBase context;
      ImageBitmap bitmap;
      const ReachImageBitmapOriginal::Unpack creation{bits & 1, (bits >> 1) & 1,
                                                      bits & 4 ? 37444 : 0};
      const ReachImageBitmapOriginal::Unpack saved{
          (bits >> 3) & 1, (bits >> 4) & 1, bits & 32 ? 37444 : 0};
      context.unpack_flip_y_ = saved[0];
      context.unpack_premultiply_alpha_ = saved[1];
      context.unpack_colorspace_conversion_ = saved[2];
      bitmap.image->original = ReachImageBitmapOriginal::Create(
          aggregate, {137, 80, 78, 71}, creation);
      TexImageParams params;
      params.function_id = function;
      const std::string op =
          function == kTexImage2D ? "texImage2D" : "texSubImage2D";
      context.capture.Reset(op);
      context.TexImageHelperImageBitmap(params, &bitmap, exception);
      auto& command = context.capture.captured_command;
      assert(command.original &&
             "encoded ImageBitmap must use original bytes, not captured RGBA");
      assert(context.captured_rgba == 0 && context.server_uploads == 1);
      const auto& args = command.value.lists.at("args");
      const auto& reference = args.references.at(0);
      const std::vector<int> expected =
          function == kTexImage2D ? std::vector<int>{3553, 0, 6408, 6408, 5121}
                                  : std::vector<int>{3553, 0, 1, 2, 6408, 5121};
      assert(args.values == expected);
      assert(reference.numbers.at("length") == 4 &&
             reference.numbers.at("width") == 2 &&
             reference.numbers.at("height") == 2);
      assert(command.original->data() ==
             bitmap.image->original->bytes().data());
      assert(command.unpack_before == creation);
      assert(command.unpack_after == saved);
      assert(context.unpack_flip_y_ == saved[0] &&
             context.unpack_premultiply_alpha_ == saved[1] &&
             context.unpack_colorspace_conversion_ == saved[2]);
      context.capture.Append(std::move(command));
      bitmap.image->original.reset();  // Browser source closes before delivery.
      assert(aggregate > 0);
      std::string batch;
      uint64_t end = 0;
      assert(context.capture.commands_.Read(5, 0, 8 * 1024 * 1024, &batch, &end)
                 .empty());
      assert(end == 7);
      std::string expected_batch = "[";
      for (int i = 0; i < 7; ++i) {
        if (i) expected_batch += ',';
        base::DictValue value;
        base::ListValue values;
        if (i == 3) {
          value.Set("op", op);
          values.values = expected;
          base::DictValue ref;
          ref.Set("image", std::string(64, 'a'));
          values.Append(std::move(ref));
        } else {
          const std::array<int, 3> names{37440, 37441, 37443};
          const size_t index = i < 3 ? i : i - 4;
          value.Set("op", "pixelStorei");
          values.Append(names[index]);
          values.Append(i < 3 ? creation[index] : saved[index]);
        }
        value.Set("args", std::move(values));
        expected_batch += *base::WriteJson(value);
      }
      assert(batch == expected_batch + "]");
      assert(!context.capture.Applied(5, end).empty());  // Read is not sent.
      assert(context.capture.commands_.Sent(5, end).empty());
      assert(context.capture.Applied(5, end).empty());
      assert(
          context.capture.Applied(5, end).empty());  // Duplicate is harmless.
      assert(context.capture.originals_.empty() && aggregate == 0);
    }
  }
  for (int rejected = 0; rejected < 4; ++rejected) {
    WebGLRenderingContextBase context;
    ImageBitmap bitmap;
    TexImageParams params;
    if (rejected != 0)
      bitmap.image->original = ReachImageBitmapOriginal::Create(
          aggregate, {137, 80, 78, 71}, {0, 1, 37444});
    if (rejected == 1) params.width = 1;
    if (rejected == 2)
      context.unpack_color_space_ = PredefinedColorSpace::kDisplayP3;
    if (rejected == 3) params.unpack_skip_pixels = 1;
    context.TexImageHelperImageBitmap(params, &bitmap, exception);
    assert(!context.capture.captured_command.error.empty() &&
           context.captured_rgba == 0 && context.server_uploads == 1);
    context.capture.Append(std::move(context.capture.captured_command));
    assert(!context.capture.usable());
  }
  assert(aggregate == 0);
  {
    WebGLRenderingContextBase context;
    ImageBitmap bitmap;
    bitmap.image->original = ReachImageBitmapOriginal::Create(
        aggregate, {137, 80, 78, 71}, {0, 1, 37444});
    for (uint64_t cursor : {uint64_t{0}, uint64_t{7}}) {
      context.capture.Reset("texImage2D");
      context.TexImageHelperImageBitmap({}, &bitmap, exception);
      context.capture.Append(std::move(context.capture.captured_command));
      std::string batch;
      uint64_t end = cursor;
      assert(context.capture.commands_.Read(5, cursor, 4096, &batch, &end)
                 .empty());
      assert(end == cursor + 7);
      assert(context.capture.commands_.Sent(5, end).empty());
      assert(context.capture.Applied(5, end).empty());
      assert(context.capture.originals_.empty() && aggregate > 0);
    }
    bitmap.image->original.reset();
    assert(aggregate == 0);  // Reupload uses current source, not old commands.
  }
  {
    WebGLRenderingContextBase context;
    context.reach_journal_ = nullptr;
    ImageBitmap bitmap;
    context.TexImageHelperImageBitmap({}, &bitmap, exception);
    assert(context.server_uploads == 1 && context.captured_rgba == 0);
  }
  {
    std::atomic_size_t crowded{ReachImageBitmapOriginal::kLimit - 1024};
    assert(!ReachImageBitmapOriginal::Create(
        crowded, std::vector<uint8_t>(1024), {0, 1, 37444}));
    assert(crowded == ReachImageBitmapOriginal::kLimit - 1024);
    assert(!ReachImageBitmapOriginal::Create(crowded, {}, {0, 1, 37444}));
  }
  std::cout << "Pinned native ImageBitmap: 120/90 MiB simultaneous admission; "
               "128 original/policy/retirement "
               "cases; reupload, explicit rejection and shared budget PASS\n";
}
