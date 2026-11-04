#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

template <typename T>
using CefRefPtr = std::shared_ptr<T>;

struct CefBinaryValue {
  static auto Create(const void* data, size_t size) {
    auto result = std::make_shared<CefBinaryValue>();
    auto* bytes = static_cast<const uint8_t*>(data);
    result->bytes.assign(bytes, bytes + size);
    return result;
  }
  std::vector<uint8_t> bytes;
};
struct CefListValue;
struct CefDictionaryValue;
struct Value {
  enum Type { kNull, kInt, kString, kBinary, kDictionary, kList } type = kNull;
  int integer = 0;
  std::string text;
  CefRefPtr<CefBinaryValue> binary;
  CefRefPtr<CefDictionaryValue> dictionary;
  CefRefPtr<CefListValue> list;
};
struct CefDictionaryValue {
  static auto Create() { return std::make_shared<CefDictionaryValue>(); }
  void SetInt(const std::string& key, int value) {
    fields[key].type = Value::kInt;
    fields[key].integer = value;
  }
  void SetString(const std::string& key, const std::string& value) {
    fields[key].type = Value::kString;
    fields[key].text = value;
  }
  void SetBinary(const std::string& key, CefRefPtr<CefBinaryValue> value) {
    fields[key].type = Value::kBinary;
    fields[key].binary = std::move(value);
  }
  std::map<std::string, Value> fields;
};
struct CefListValue {
  static auto Create() { return std::make_shared<CefListValue>(); }
  void SetSize(size_t size) { fields.resize(size); }
  void SetNull(size_t index) {
    if (fields.size() <= index)
      fields.resize(index + 1);
    fields[index] = {};
  }
  void SetDictionary(size_t index, CefRefPtr<CefDictionaryValue> value) {
    if (fields.size() <= index)
      fields.resize(index + 1);
    fields[index].type = Value::kDictionary;
    fields[index].dictionary = std::move(value);
  }
  void SetList(size_t index, CefRefPtr<CefListValue> value) {
    if (fields.size() <= index)
      fields.resize(index + 1);
    fields[index].type = Value::kList;
    fields[index].list = std::move(value);
  }
  std::vector<Value> fields;
};
namespace blink_glue {
struct CefCanvasVideoFrame {
  uint32_t id;
  std::string document, stream, generation, sequence;
  int width, height;
  std::vector<uint8_t> rgba;
};
struct CefDrawingCommands {
  std::optional<std::vector<CefCanvasVideoFrame>> canvas_video;
  std::vector<uint8_t> scroll;
};
}  // namespace blink_glue

#include "reach_canvas_video_ipc.inc"

int main() {
  blink_glue::CefDrawingCommands capture;
  auto commands = CefListValue::Create();
  commands->SetSize(6);
  AttachCanvasVideo(commands, capture, false);
  assert(commands->fields.size() == 6);
  capture.canvas_video.emplace();
  AttachCanvasVideo(commands, capture, false);
  assert(commands->fields.size() == 10);
  assert(commands->fields[9].type == Value::kList);
  assert(commands->fields[9].list->fields.empty());
  for (size_t index = 6; index <= 8; ++index)
    assert(commands->fields[index].type == Value::kNull);
  capture.canvas_video.emplace();
  capture.canvas_video->push_back(
      {7, "document", "9", "2", "3", 1, 1, {1, 2, 3, 17}});
  auto incremental = CefListValue::Create();
  incremental->SetSize(9);
  incremental->fields[8].type = Value::kBinary;
  capture.scroll.push_back(1);
  AttachCanvasVideo(incremental, capture, true);
  assert(incremental->fields[8].type == Value::kBinary);
  const auto& record =
      incremental->fields[9].list->fields[0].dictionary->fields;
  assert(record.size() == 8);
  assert(record.at("id").type == Value::kInt);
  assert(record.at("id").integer == 7);
  assert(record.at("stream").type == Value::kString);
  assert(record.at("stream").text == "9");
  assert(record.at("document").text == "document");
  assert(record.at("rgba").type == Value::kBinary);
  assert(record.at("rgba").binary->bytes[3] == 17);
  // Release the response's raw inventory before IPC serialization/receiver
  // copy.
  assert(!capture.canvas_video);
  std::cout
      << "actual typed IPC packing, empty inventory and copy release passed\n";
}
