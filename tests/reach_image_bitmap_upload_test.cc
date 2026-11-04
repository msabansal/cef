#include <cassert>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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
namespace base {
template <typename T>
using span = std::span<T>;
struct DictValue;
struct ListValue {
  std::vector<int> values;
  std::vector<DictValue> references;
  void Append(int value) { values.push_back(value); }
  void Append(DictValue);
};
struct DictValue {
  std::map<std::string, std::string> strings;
  std::map<std::string, double> numbers;
  std::map<std::string, ListValue> lists;
  void Set(std::string key, std::string value) {
    strings.emplace(std::move(key), std::move(value));
  }
  void Set(std::string key, double value) {
    numbers.emplace(std::move(key), value);
  }
  void Set(std::string key, ListValue value) {
    lists.emplace(std::move(key), std::move(value));
  }
  const std::string* FindString(const std::string& key) const {
    auto it = strings.find(key);
    return it == strings.end() ? nullptr : &it->second;
  }
};
void ListValue::Append(DictValue value) {
  references.push_back(std::move(value));
}
std::optional<std::string> WriteJson(const DictValue& value) {
  const auto& args = value.lists.at("args");
  std::string json = "{\"op\":\"" + value.strings.at("op") + "\",\"args\":[";
  for (int number : args.values)
    json += (json.back() == '[' ? "" : ",") + std::to_string(number);
  for (const auto& reference : args.references) {
    json += ",{\"image\":\"" + reference.strings.at("image") + "\"}";
  }
  return json + "]}";
}
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
  assert((std::vector<uint8_t>(bytes.begin(), bytes.end()) ==
          std::vector<uint8_t>{137, 80, 78, 71}));
  return std::string(64, 'a');
}
auto original_hasher = &Hash;
constexpr size_t kMaxCommandBytes = 4 * 1024 * 1024;
struct ReachWebGLCommand {
  base::DictValue value;
  std::string error;
  bool texture_source = false;
  std::string original_hash;
  sk_sp<const SkData> original;
  std::optional<ReachImageBitmapOriginal::Unpack> unpack_before, unpack_after;
};
class ReachWebGLJournal {
 public:
  struct Original {
    sk_sp<const SkData> data;
    uint64_t last_next;
  };
  static constexpr size_t kMaxOriginalBytes =
      ReachImageBitmapOriginal::kMaxBytes;
  ReachWebGLCommand captured_command;
  ReachWebGLCommand* pending_ = &captured_command;
  ReachWebGLPending commands_{aggregate, 5};
  std::map<std::string, Original> originals_;
  size_t original_bytes_ = 0;
  uint64_t generation_ = 5;
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
  void OriginalImageBitmapTexture(
      base::ListValue, std::shared_ptr<const ReachImageBitmapOriginal>, int,
      int, ReachImageBitmapOriginal::Unpack);
  void Append(ReachWebGLCommand);
  std::string Applied(uint64_t, uint64_t);
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
  std::cout << "Pinned native ImageBitmap: 128 original/policy/retirement "
               "cases; reupload, explicit rejection and shared budget PASS\n";
}
