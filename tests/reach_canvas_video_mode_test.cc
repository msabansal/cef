#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#ifdef _MSC_VER
#pragma warning(disable : 4100)
#else
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

namespace base {
struct Environment {
  static auto Create() { return std::make_unique<Environment>(); }
  std::optional<std::string> GetVar(const std::string& name) {
    return values.at(name);
  }
  static inline std::map<std::string, std::string> values;
};
struct CommandLine {
  static auto* ForCurrentProcess() {
    static CommandLine command;
    return &command;
  }
  bool HasSwitch(const std::string& name) const {
    return switches.contains(name);
  }
  std::set<std::string> switches;
};
}  // namespace base

namespace blink {
struct Canvas2DResourceProvider {
  static bool IsReachCommandMode();
  static bool IsReachCanvasVideoMode();
};
#include "reach_canvas_video_mode.inc"

template <typename T>
using scoped_refptr = std::shared_ptr<T>;
struct StaticBitmapImage {};
enum SourceDrawingBuffer { kBackBuffer };
enum class FlushReason { kOther };
struct BaseRenderingContext2D {
  bool IsBackingValid() const { return valid; }
  void FlushCanvas(FlushReason) { ++flushes; }
  scoped_refptr<StaticBitmapImage> Snapshot() { return image; }
  scoped_refptr<StaticBitmapImage> PaintRenderingResultsToSnapshot(
      SourceDrawingBuffer source_buffer);
  bool valid = false;
  unsigned flushes = 0;
  scoped_refptr<StaticBitmapImage> image =
      std::make_shared<StaticBitmapImage>();
};
struct CanvasRenderingContext2D : BaseRenderingContext2D {
  void* GetOrCreatePaintCanvas() {
    ++creates;
    valid = create_ok;
    return valid ? this : nullptr;
  }
  scoped_refptr<StaticBitmapImage> PaintRenderingResultsToSnapshot(
      SourceDrawingBuffer source_buffer);
  bool create_ok = true;
  unsigned creates = 0;
};
#include "reach_canvas_video_snapshot.inc"
}  // namespace blink

int main(int argc, char** argv) {
  assert(argc == 6);
  auto* line = base::CommandLine::ForCurrentProcess();
  if (std::string(argv[1]) == "1")
    line->switches.insert("reach-drawing-commands");
  if (std::string(argv[2]) == "1")
    line->switches.insert("reach-webgl-video");
  base::Environment::values["REACH_WEBGL_VIDEO"] = argv[3];
  base::Environment::values["REACH_WEBGL_COMMAND_REPLAY"] = argv[4];
  assert(blink::Canvas2DResourceProvider::IsReachCanvasVideoMode() ==
         (std::string(argv[5]) == "1"));
  assert(blink::Canvas2DResourceProvider::IsReachCommandMode() ==
         (std::string(argv[1]) == "1"));
  const bool command = std::string(argv[1]) == "1";
  blink::CanvasRenderingContext2D empty;
  const auto empty_image =
      empty.PaintRenderingResultsToSnapshot(blink::kBackBuffer);
  assert(bool(empty_image) == command);
  assert(empty.creates == (command ? 1u : 0u));
  assert(empty.flushes == (command ? 1u : 0u));
  blink::CanvasRenderingContext2D backed;
  backed.valid = true;
  assert(backed.PaintRenderingResultsToSnapshot(blink::kBackBuffer) ==
         backed.image);
  assert(backed.creates == 0 && backed.flushes == 1);
  blink::CanvasRenderingContext2D failed;
  failed.create_ok = false;
  assert(!failed.PaintRenderingResultsToSnapshot(blink::kBackBuffer));
  assert(failed.creates == (command ? 1u : 0u));
  assert(failed.flushes == 0);
}
