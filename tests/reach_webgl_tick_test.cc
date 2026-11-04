#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace base {
struct TimeTicks {
  static TimeTicks Now() { return {}; }
};
}  // namespace base

namespace blink {
struct Pinned {
  int pins = 0;
};
template <typename T>
class Persistent {
 public:
  Persistent(T* ptr) : ptr_(ptr) {
    if (ptr_) ++ptr_->pins;
  }
  ~Persistent() {
    if (ptr_) --ptr_->pins;
  }
  T* Get() const { return ptr_; }
  T* operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  T* ptr_;
};
struct LocalFrame;
struct Document : Pinned {
  struct TokenValue {
    std::string ToString() const { return "same-token"; }
  };
  TokenValue Token() const { return {}; }
  LocalFrame* GetFrame() const { return owner; }
  LocalFrame* owner = nullptr;
};
struct AnimationDriver {
  std::function<void()> script;
  std::function<void()> lifecycle;
  void ServiceScriptedAnimations(base::TimeTicks) { script(); }
  template <typename Reason>
  void UpdateAllLifecyclePhasesExceptPaint(LocalFrame&, Reason) {
    lifecycle();
  }
};
struct Page : Pinned {
  AnimationDriver& Animator() { return driver; }
  AnimationDriver driver;
};
struct LocalFrame : Pinned {
  bool IsAttached() const { return attached; }
  Document* GetDocument() const { return current_document; }
  Page* GetPage() const { return current_page; }
  bool View() const { return true; }
  LocalFrame& LocalFrameRoot() { return *this; }
  Document* current_document = nullptr;
  Page* current_page = nullptr;
  bool attached = true;
};
struct WebLocalFrame {};
struct WebLocalFrameImpl : WebLocalFrame {
  LocalFrame* GetFrame() const { return target; }
  LocalFrame* target = nullptr;
};
template <typename T>
T* To(WebLocalFrame* value) {
  return static_cast<T*>(value);
}
struct Canvas2DBitmapProvider {
  static bool IsReachWebGLCommandMode() { return true; }
};
struct HTMLCanvasElement {
  Document& GetDocument() const { return *document; }
  Document* document = nullptr;
};
struct ReachWebGLJournal {
  HTMLCanvasElement* element() const { return nullptr; }
};
enum class DocumentUpdateReason { kInspector };
namespace scheduler {
struct MainThreadSchedulerImpl {
  void BeginFrameNotExpectedSoon() {}
};
struct WebThreadScheduler {
  static MainThreadSchedulerImpl& MainThreadScheduler() {
    static MainThreadSchedulerImpl scheduler;
    return scheduler;
  }
};
}  // namespace scheduler
}  // namespace blink

// NATIVE_WEBGL_REQUEST_PREFIX

int main() {
  // Both documents deliberately report the same token; address identity matters.
  for (int changed_at = 0; changed_at < 3; ++changed_at) {
    blink::Document original;
    blink::Document replacement;
    blink::Page page;
    blink::LocalFrame frame;
    frame.current_document = &original;
    frame.current_page = &page;
    original.owner = &frame;
    replacement.owner = &frame;
    blink::WebLocalFrameImpl web;
    web.target = &frame;
    int scripts = 0;
    int lifecycles = 0;
    auto check_pins = [&] {
      assert(original.pins == 1 && page.pins == 1 && frame.pins == 1);
      assert(replacement.pins == 0);
    };
    page.driver.script = [&] {
      check_pins();
      ++scripts;
      if (changed_at == 1) frame.current_document = &replacement;
    };
    page.driver.lifecycle = [&] {
      check_pins();
      ++lifecycles;
      if (changed_at == 2) frame.current_document = &replacement;
    };
    std::vector<uint8_t> output;
    std::map<std::string, std::vector<uint8_t>> originals;
    const std::string token = "same-token";
    const auto result = WebGlRequest(
        &web, "Reach.WebGlTick.v2", token, "", "", "", "", 0,
        &output, &originals);
    assert(result == (changed_at ? "WebGL animation document changed" : ""));
    assert(scripts == 1 && lifecycles == (changed_at == 1 ? 0 : 1));
    assert(original.pins == 0 && replacement.pins == 0);
    assert(page.pins == 0 && frame.pins == 0 && token == "same-token");
    assert(WebGlRequest(&web, "Reach.WebGlRead.v2", "stale-token", "", "", "",
                        "", 0, &output, &originals) ==
           "WebGL native document identity is stale");
  }
}
