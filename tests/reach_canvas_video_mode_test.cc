#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

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
struct Canvas2DBitmapProvider {
  static bool IsReachCommandMode();
  static bool IsReachWebGLCommandMode();
  static bool IsReachWebGLVideoMode();
};
#include "reach_canvas_video_mode.inc"
}  // namespace blink

int main(int argc, char** argv) {
  assert(argc == 7);
  auto* line = base::CommandLine::ForCurrentProcess();
  if (std::string(argv[1]) == "1")
    line->switches.insert("reach-drawing-commands");
  if (std::string(argv[2]) == "1")
    line->switches.insert("reach-webgl-video");
  base::Environment::values["REACH_WEBGL_VIDEO"] = argv[3];
  base::Environment::values["REACH_WEBGL_COMMAND_REPLAY"] = argv[4];
  assert(blink::Canvas2DBitmapProvider::IsReachWebGLVideoMode() ==
         (std::string(argv[5]) == "1"));
  assert(blink::Canvas2DBitmapProvider::IsReachWebGLCommandMode() ==
         (std::string(argv[6]) == "1"));
}
