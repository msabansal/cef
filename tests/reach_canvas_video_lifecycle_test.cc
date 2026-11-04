#include "reach_canvas_video.h"

#include <cassert>
#include <iostream>

#define FROM_HERE 0
#define DCHECK_NE(left, right) assert((left) != (right))

namespace base {
int TimeDelta() {
  return 0;
}
}  // namespace base

namespace blink {
constexpr int GL_INVALID_OPERATION = 1;
constexpr int GC3D_CONTEXT_LOST_WEBGL = 2;
enum class DOMExceptionCode { kInvalidStateError };
struct DrawingBuffer {};
struct ExtensionTracker {
  void LoseExtension(bool) {}
};
template <typename... Args>
int BindOnce(Args&&...) {
  return 0;
}
template <typename T>
T* WrapWeakPersistent(T* value) {
  return value;
}
template <typename T>
const T& RetainedRef(const T& value) {
  return value;
}
struct TaskRunner {
  void PostTask(int, int) {}
};
struct Timer {
  void StartOneShot(int, int) {}
};
struct LayoutObject {
  void SetShouldDoFullPaintInvalidation() { invalidated = true; }
  bool invalidated = false;
};
struct HTMLCanvasElement {
  bool IsOffscreenCanvas() const { return false; }
  void NotifyGpuContextLost() {}
  LayoutObject* GetLayoutObject() { return &layout; }
  LayoutObject layout;
};
struct Canvas2DBitmapProvider {
  static bool IsReachWebGLVideoMode() { return true; }
};
void (*reach_canvas_video_change_callback)(HTMLCanvasElement*) = nullptr;

struct CanvasRenderingContext {
  bool IsWebGL() const { return true; }
  HTMLCanvasElement* Host() { return &host; }
  void ReachCanvasVideoChanged(bool epoch);
  void ReleaseReachCanvasVideo();
  std::unique_ptr<ReachCanvasVideo> reach_canvas_video_;
  HTMLCanvasElement host;
};

struct WebGLRenderingContextBase : CanvasRenderingContext {
  enum LostContextMode {
    kNotLostContext,
    kRealLostContext,
    kWebGLLoseContextLostContext
  };
  enum AutoRecoveryMethod { kManual, kWhenAvailable };
  enum ConsoleDisplayPreference { kDisplayInConsole, kDontDisplayInConsole };
  bool isContextLost() const { return context_lost_mode_ != kNotLostContext; }
  void SynthesizeGLError(int,
                         const char*,
                         const char*,
                         ConsoleDisplayPreference = kDontDisplayInConsole) {}
  void CompleteXrCompatiblePromiseIfPending(DOMExceptionCode) {}
  void RemoveAllCompressedTextureFormats() {}
  TaskRunner* GetContextTaskRunner() { return &runner; }
  void HoldReferenceToDrawingBuffer(DrawingBuffer*) {}
  void DestroyContext() { drawing_buffer_.reset(); }
  void DeactivateContext(WebGLRenderingContextBase*) {}
  void AddToEvictedList(WebGLRenderingContextBase*) {}
  void ForceLostContext(LostContextMode, AutoRecoveryMethod);
  LostContextMode context_lost_mode_ = kNotLostContext;
  AutoRecoveryMethod auto_recovery_method_ = kManual;
  std::vector<ExtensionTracker*> extensions_;
  bool xr_compatible_ = false;
  bool restore_allowed_ = true;
  std::shared_ptr<DrawingBuffer> drawing_buffer_ =
      std::make_shared<DrawingBuffer>();
  TaskRunner runner;
  Timer dispatch_context_lost_event_timer_;
};

#include "reach_canvas_video_lifecycle.inc"
}  // namespace blink

int main() {
  using Context = blink::WebGLRenderingContextBase;
  for (const auto mode :
       {Context::kRealLostContext, Context::kWebGLLoseContextLostContext}) {
    Context context;
    context.ReachCanvasVideoChanged(true);
    assert(context.host.layout.invalidated);
    context.host.layout.invalidated = false;
    context.ReachCanvasVideoChanged(false);
    assert(!context.host.layout.invalidated);
    auto budget = std::make_shared<blink::ReachCanvasVideo::Budget>();
    auto& original = *context.reach_canvas_video_;
    original.SetBudget(budget);
    assert(original.Checkpoint(
        1, 1, [] { return std::vector<uint8_t>{255, 0, 0, 255}; }));
    const auto retired_stream = original.stream();
    assert(budget->active.size() == 1);
    assert(!context.host.layout.invalidated);

    context.ForceLostContext(mode, Context::kManual);
    assert(context.isContextLost());
    assert(!context.reach_canvas_video_);
    assert(budget->active.empty());
    assert(context.host.layout.invalidated);
    context.ForceLostContext(mode, Context::kManual);
    assert(!context.reach_canvas_video_);

    // Blink restores the drawing buffer on the same rendering-context object.
    context.context_lost_mode_ = Context::kNotLostContext;
    context.drawing_buffer_ = std::make_shared<blink::DrawingBuffer>();
    context.host.layout.invalidated = false;
    context.ReachCanvasVideoChanged(true);
    assert(context.host.layout.invalidated);
    auto& restored = *context.reach_canvas_video_;
    assert(restored.stream() != retired_stream);
    assert(restored.Checkpoint(
        1, 1, [] { return std::vector<uint8_t>{0, 255, 0, 255}; }));
    assert(restored.sequence() == 1 && restored.rgba()[1] == 255);
  }
  std::cout << "actual context-loss hook retires stream and restored producer "
               "has a fresh independent identity\n";
}
