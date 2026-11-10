#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace base {
template <typename T>
struct NoDestructor {
  T value;
  T& operator*() { return value; }
};
inline std::string NumberToString(uint64_t value) {
  return std::to_string(value);
}
struct TimeTicks {
  static TimeTicks Now() { return {}; }
};
}  // namespace base

namespace blink {
struct Visitor {};
template <typename T>
struct GarbageCollected {};
template <typename T, typename... Args>
T* MakeGarbageCollected(Args&&... args) {
  return new T(std::forward<Args>(args)...);
}
template <typename T>
struct Supplement {
  explicit Supplement(T&) {}
  virtual ~Supplement() = default;
  virtual void Trace(Visitor*) const {}
  template <typename S>
  static S* From(T& object) {
    return static_cast<S*>(object.supplement);
  }
  static void ProvideTo(T& object, Supplement* value) {
    object.supplement = value;
  }
};
struct Pinned {
  int pins = 0;
};
template <typename T>
class Persistent {
 public:
  Persistent(T* ptr) : ptr_(ptr) {
    if (ptr_)
      ++ptr_->pins;
  }
  ~Persistent() {
    if (ptr_)
      --ptr_->pins;
  }
  T* Get() const { return ptr_; }
  T* operator->() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  T* ptr_;
};
struct LocalFrame;
struct Document : Pinned {
  void* supplement = nullptr;
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
  static WebLocalFrame* FromFrame(LocalFrame&) {
    static WebLocalFrameImpl web;
    return &web;
  }
  LocalFrame* GetFrame() const { return target; }
  LocalFrame* target = nullptr;
};
template <typename T>
T* To(WebLocalFrame* value) {
  return static_cast<T*>(value);
}
struct Canvas2DBitmapProvider {
  static bool IsReachCommandMode() { return enabled; }
  static inline bool enabled = true;
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

// NATIVE_DEMAND_NOTIFICATIONS

// NATIVE_ANIMATION_REQUEST

int main() {
  {
    blink::Document document;
    blink::Document replacement;
    blink::Page page;
    blink::LocalFrame frame;
    frame.current_document = &document;
    frame.current_page = &page;
    document.owner = &frame;
    replacement.owner = &frame;
    blink::WebLocalFrameImpl web;
    web.target = &frame;
    int pictures = 0;
    int wakes = 0;
    FrameRequestCallback() = [&](blink::WebLocalFrame*) { ++pictures; };
    AnimationCallback() = [&](blink::WebLocalFrame*) { ++wakes; };
    auto inventory = [&] {
      std::vector<uint8_t> bytes;
      assert(DrawingAnimationRequest(&web, false, "", &bytes).empty());
      return std::string(bytes.begin(), bytes.end());
    };
    const auto idle = inventory();
    // A fully idle ordinary page, with no canvas contexts, asks for its first RAF.
    OnFrameRequested(frame);
    assert(wakes == 1 && pictures == 1);
    assert(idle.find("\"animationPending\":false") != std::string::npos);
    OnFrameRequested(frame);
    assert(wakes == 1 && pictures == 1);
    assert(inventory().find("\"animationPending\":true") != std::string::npos);
    assert(inventory().find("\"animationPending\":true") != std::string::npos);
    int scripts = 0;
    bool rearm = true;
    page.driver.script = [&] {
      ++scripts;
      if (rearm)
        OnFrameRequested(frame);
    };
    page.driver.lifecycle = [] {};
    auto tick = [&] {
      std::vector<uint8_t> bytes;
      assert(DrawingAnimationRequest(&web, true, "same-token", &bytes).empty());
    };
    tick();
    assert(wakes == 2 && scripts == 1 && pictures == 1);
    assert(inventory().find("\"animationPending\":true") != std::string::npos);
    rearm = false;
    tick();
    assert(scripts == 2);
    assert(inventory().find("\"animationPending\":false") != std::string::npos);
    // Lifecycle scheduling (including CSS), not just script, rearms demand.
    page.driver.lifecycle = [&] { OnFrameRequested(frame); };
    tick();
    assert(wakes == 3);
    assert(inventory().find("\"animationPending\":true") != std::string::npos);
    page.driver.lifecycle = [] {};
    tick();
    assert(inventory().find("\"animationPending\":false") != std::string::npos);
    OnFrameRequested(frame);
    assert(wakes == 4);
    frame.current_document = &replacement;
    assert(inventory().find("\"animationPending\":false") != std::string::npos);
    OnFrameRequested(frame);
    assert(wakes == 5);
    assert(pictures == 2);
    frame.current_document = &document;
    assert(inventory().find("\"animationPending\":true") != std::string::npos);
    // Drawing notification coalescing cannot swallow a new animation wake.
    assert(pictures == 2);
  }
  AnimationCallback() = {};
  FrameRequestCallback() = {};
  // Both documents deliberately report the same token; address identity
  // matters.
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
      if (changed_at == 1)
        frame.current_document = &replacement;
    };
    page.driver.lifecycle = [&] {
      check_pins();
      ++lifecycles;
      if (changed_at == 2)
        frame.current_document = &replacement;
    };
    std::vector<uint8_t> output;
    const std::string token = "same-token";
    const auto result = DrawingAnimationRequest(&web, true, token, &output);
    assert(result == (changed_at ? "Animation document changed" : ""));
    assert(scripts == 1 && lifecycles == (changed_at == 1 ? 0 : 1));
    assert(original.pins == 0 && replacement.pins == 0);
    assert(page.pins == 0 && frame.pins == 0 && token == "same-token");
    const auto before = scripts;
    for (const bool tick : {false, true}) {
      assert(DrawingAnimationRequest(&web, tick, "stale-token", &output) ==
             "Animation request document identity is stale");
    }
    assert(DrawingAnimationRequest(&web, true, "", &output) ==
           "Animation request document identity is stale");
    assert(scripts == before);
    blink::Canvas2DBitmapProvider::enabled = false;
    assert(DrawingAnimationRequest(&web, false, "", &output) ==
           "Animation request frame is detached or disabled");
    blink::Canvas2DBitmapProvider::enabled = true;
  }
}
