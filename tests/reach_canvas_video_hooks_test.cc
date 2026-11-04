#include "reach_canvas_video.h"

#include <cassert>
#include <iostream>

#ifdef _MSC_VER
#pragma warning(disable : 4100)
#else
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

namespace gfx {
struct Rect {
  explicit Rect(int value = 0) : empty(value == 0) {}
  bool IsEmpty() const { return empty; }
  void Intersect(const Rect&) {}
  bool empty;
};
}  // namespace gfx

template <typename T>
using scoped_refptr = std::shared_ptr<T>;

namespace base {
template <typename T, typename... Args>
auto MakeRefCounted(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}
}  // namespace base

namespace blink {
constexpr unsigned GL_RGB8 = 0x8051;
constexpr unsigned GL_RGBA8 = 0x8058;
constexpr unsigned GL_SRGB8_ALPHA8 = 0x8c43;
constexpr unsigned GL_RGBA16F = 0x881a;
struct ColorSpace {
  bool IsHDR() const { return hdr; }
  bool hdr = false;
};
enum FlushReason { kTest };
enum SourceDrawingBuffer { kBackBuffer };
struct CanvasPerformanceMonitor {
  enum DrawType { kOther };
};
struct CanvasResource {};
struct ExportedCanvasResource {
  explicit ExportedCanvasResource(std::shared_ptr<CanvasResource>) {}
};
struct Layer {
  void SetNeedsDisplay() {}
};
struct Framebuffer {
  void SetContentsChanged(bool) {}
};
struct HostFixture {
  bool IsOffscreenCanvas() const { return false; }
};

struct WebGLRenderingContextBase {
  enum ContentChangeType { kCanvasChanged };
  struct Buffer {
    bool MarkContentsChanged() {
      const bool changed = !dirty;
      dirty = true;
      return changed;
    }
    void SetBufferClearNeeded(bool value) { clear_needed = value; }
    bool dirty = false;
    bool clear_needed = false;
  } buffer;

  bool isContextLost() const { return lost; }
  unsigned drawingBufferFormat() const { return storage_format; }
  const ColorSpace& GetColorSpace() const { return color_space; }
  bool ReachCanvasVideoIsSDR() const;
  Buffer* GetDrawingBuffer() { return &buffer; }
  HostFixture* Host() { return &host; }
  bool canvas() const { return true; }
  Layer* CcLayer() { return &layer; }
  void DidDraw(CanvasPerformanceMonitor::DrawType) {}
  void ReachCanvasVideoChanged() { capture.Dirty(); }
  void MarkContextChanged(ContentChangeType,
                          CanvasPerformanceMonitor::DrawType);
  void MarkLayerComposited();
  bool CheckpointReachCanvasVideo() {
    return capture.Checkpoint(1, 1, [&] {
      ++reads;
      if (buffer.clear_needed)
        pixels.assign(4, 0);
      return pixels;
    });
  }
  void ClearMarkedCanvasDirty() { marked_canvas_dirty_ = false; }
  std::shared_ptr<CanvasResource> PaintRenderingResultsToResource(
      SourceDrawingBuffer,
      FlushReason) {
    // Export itself may mark a drawing buffer as composited.
    buffer.clear_needed = true;
    MarkLayerComposited();
    return std::make_shared<CanvasResource>();
  }
  blink::ReachCanvasVideo capture;
  std::vector<uint8_t> pixels{1, 2, 3, 17};
  unsigned reads = 0;
  bool lost = false;
  unsigned storage_format = GL_RGBA8;
  ColorSpace color_space;
  Framebuffer* framebuffer_binding_ = nullptr;
  bool must_paint_to_canvas_ = false;
  bool marked_canvas_dirty_ = false;
  HostFixture host;
  Layer layer;
};

struct Dispatcher {
  void DispatchFrame(std::shared_ptr<ExportedCanvasResource>,
                     const gfx::Rect&,
                     bool) {
    ++frames;
  }
  unsigned frames = 0;
};
struct Accessibility {
  void UpdateAnnotation() {}
};
struct HTMLCanvasElement {
  void PostFinalizeFrame(FlushReason);
  void RecordCanvasSizeToUMA() {}
  bool IsWebGL() const { return true; }
  bool LowLatencyEnabled() const { return true; }
  bool IsRenderingContext2D() const { return false; }
  int Size() const { return 1; }
  bool IsOpaque() const { return false; }
  void NotifyListenersCanvasChanged() {}
  void NotifyCachesOfSwitchingFrame() {}
  WebGLRenderingContextBase* context_ = nullptr;
  Dispatcher* frame_dispatcher_ = nullptr;
  gfx::Rect dirty_rect_{1};
  bool did_notify_listeners_for_current_frame_ = false;
  Accessibility* accessibility_manager_ = nullptr;
  bool should_capture_rendered_text_ = false;
};

#include "reach_canvas_video_hooks.inc"
}  // namespace blink

int main() {
  using Context = blink::WebGLRenderingContextBase;
  Context context;
  for (const auto format :
       {blink::GL_RGB8, blink::GL_RGBA8, blink::GL_SRGB8_ALPHA8}) {
    context.storage_format = format;
    assert(context.ReachCanvasVideoIsSDR());
    context.color_space.hdr = true;
    assert(!context.ReachCanvasVideoIsSDR());
    context.color_space.hdr = false;
  }
  context.storage_format = blink::GL_RGBA16F;
  assert(!context.ReachCanvasVideoIsSDR());
  context.storage_format = 0;
  assert(!context.ReachCanvasVideoIsSDR());
  context.storage_format = blink::GL_RGBA8;
  context.MarkLayerComposited();
  assert(context.CheckpointReachCanvasVideo());
  assert(context.capture.rgba()[3] == 17);
  assert(context.reads == 1);
  // User FBO changes must not invalidate the default-buffer snapshot.
  blink::Framebuffer fbo;
  context.framebuffer_binding_ = &fbo;
  context.MarkContextChanged(Context::kCanvasChanged,
                             blink::CanvasPerformanceMonitor::kOther);
  assert(context.CheckpointReachCanvasVideo() && context.reads == 1);
  // Default-buffer changes must bypass Blink's existing dirty coalescing.
  context.framebuffer_binding_ = nullptr;
  context.marked_canvas_dirty_ = true;
  context.buffer.dirty = true;
  context.buffer.clear_needed = false;
  context.pixels[3] = 81;
  context.MarkContextChanged(Context::kCanvasChanged,
                             blink::CanvasPerformanceMonitor::kOther);
  assert(context.CheckpointReachCanvasVideo());
  assert(context.capture.rgba()[3] == 81 && context.reads == 2);
  Context early;
  blink::Dispatcher dispatcher;
  blink::HTMLCanvasElement canvas;
  canvas.context_ = &early;
  canvas.frame_dispatcher_ = &dispatcher;
  canvas.PostFinalizeFrame(blink::kTest);
  assert(dispatcher.frames == 1);
  assert(early.CheckpointReachCanvasVideo());
  assert(early.capture.rgba()[3] == 17 && early.reads == 1);
  std::cout << "actual dirty, pre-discard and early-export hooks passed\n";
}
