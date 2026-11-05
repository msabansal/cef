#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define FROM_HERE 0
template <typename T>
using scoped_refptr = std::shared_ptr<T>;

bool in_gc = false;
std::function<void()> allocation_hook;

namespace base {
class SingleThreadTaskRunner {
 public:
  static scoped_refptr<SingleThreadTaskRunner> GetCurrentDefault() {
    static auto runner = std::make_shared<SingleThreadTaskRunner>();
    return runner;
  }
  void PostTask(int, std::function<void()> task) {
    tasks.push_back(std::move(task));
  }
  void RunUntilIdle() {
    assert(!in_gc);
    auto ready = std::exchange(tasks, {});
    for (auto& task : ready)
      task();
    assert(tasks.empty());
  }
  std::vector<std::function<void()>> tasks;
};
template <typename F, typename T>
auto BindOnce(F function, T value) {
  return [function, value] { function(value); };
}
struct DictValue {
  template <typename T>
  void Set(const char*, T&&) {}
};
struct ListValue {
  void Append(int) {}
};
}  // namespace base

namespace blink {
struct Visitor {};
template <typename T>
struct GarbageCollected {};
template <typename T, typename... Args>
T* MakeGarbageCollected(Args&&... args) {
  assert(!in_gc);
  if (allocation_hook)
    allocation_hook();
  return new T(std::forward<Args>(args)...);
}
template <typename T>
struct Supplement {
  explicit Supplement(T&) {}
  virtual ~Supplement() = default;
  virtual void Trace(Visitor*) const {}
  template <typename S>
  static S* From(T& object) {
    assert(!in_gc);
    return static_cast<S*>(object.supplement.get());
  }
  static void ProvideTo(T& object, Supplement* value) {
    assert(!in_gc);
    object.supplement.reset(value);
  }
};
template <typename T>
struct WeakPersistent {
  WeakPersistent() : slot(std::make_shared<T*>(nullptr)) {}
  WeakPersistent(T* value)
      : slot(value ? value->weak : std::make_shared<T*>(nullptr)) {}
  WeakPersistent& operator=(T* value) {
    slot = value ? value->weak : std::make_shared<T*>(nullptr);
    return *this;
  }
  T* Get() const {
    assert(!in_gc);
    return *slot;
  }
  T* operator->() const { return Get(); }
  explicit operator bool() const { return Get() != nullptr; }
  bool operator==(T* value) const { return Get() == value; }
  std::shared_ptr<T*> slot;
};
template <typename T>
struct Persistent {
  Persistent(T* value) : ptr(value) {
    if (ptr)
      ++ptr->pins;
  }
  ~Persistent() {
    if (ptr)
      --ptr->pins;
  }
  T* Get() const { return ptr; }
  T* ptr;
};
struct LocalFrame;
struct Document {
  Document() : weak(std::make_shared<Document*>(this)) {}
  ~Document() { *weak = nullptr; }
  LocalFrame* GetFrame() const {
    assert(!in_gc);
    return frame;
  }
  std::shared_ptr<Document*> weak;
  std::unique_ptr<Supplement<Document>> supplement;
  LocalFrame* frame = nullptr;
  int pins = 0;
};
struct LocalFrame {
  bool IsAttached() const {
    assert(!in_gc);
    return attached;
  }
  Document* GetDocument() const {
    assert(!in_gc);
    return document;
  }
  LocalFrame& LocalFrameRoot() { return root ? *root : *this; }
  Document* document = nullptr;
  LocalFrame* root = nullptr;
  bool attached = true;
};
struct WebLocalFrame {
  LocalFrame* frame;
};
struct WebLocalFrameImpl {
  static WebLocalFrame* FromFrame(LocalFrame& frame) {
    assert(!in_gc);
    static std::map<LocalFrame*, WebLocalFrame> frames;
    return &frames.try_emplace(&frame, WebLocalFrame{&frame}).first->second;
  }
};
struct Canvas2DBitmapProvider {
  static bool IsReachWebGLVideoMode() { return false; }
};
struct HTMLCanvasElement {
  HTMLCanvasElement(Document& owner)
      : document(&owner), weak(std::make_shared<HTMLCanvasElement*>(this)) {}
  Document& GetDocument() const {
    assert(!in_gc);
    return *document;
  }
  bool IsOffscreenCanvas() const { return false; }
  Document* document;
  std::shared_ptr<HTMLCanvasElement*> weak;
  int pins = 0;
};
struct ReachWebGLCommand {
  base::DictValue value;
};
struct ReachWebGLJournal;
struct WebGLRenderingContextBase {
  explicit WebGLRenderingContextBase(HTMLCanvasElement& canvas)
      : element(canvas),
        weak(std::make_shared<WebGLRenderingContextBase*>(this)) {}
  HTMLCanvasElement* Host() const { return &element; }
  uint64_t GetContextGeneration() const { return 1; }
  int drawingBufferWidth() const { return 16; }
  int drawingBufferHeight() const { return 16; }
  ReachWebGLJournal* ReachExistingJournal() const { return journal; }
  HTMLCanvasElement& element;
  std::shared_ptr<WebGLRenderingContextBase*> weak;
  ReachWebGLJournal* journal = nullptr;
  int pins = 0;
};
std::atomic_size_t retained_bytes{0};
std::atomic_size_t& RetainedBytes() {
  return retained_bytes;
}
auto& Registry() {
  static std::map<uint64_t, WeakPersistent<WebGLRenderingContextBase>> registry;
  return registry;
}
uint64_t NextId() {
  static uint64_t next = 0;
  return ++next;
}
void (*change_callback)(HTMLCanvasElement*) = nullptr;
std::function<void()> (*retirement_callback)(HTMLCanvasElement*) = nullptr;
struct ReachWebGLJournal {
  explicit ReachWebGLJournal(WebGLRenderingContextBase&);
  ~ReachWebGLJournal();
  HTMLCanvasElement* element() const;
  static ReachWebGLJournal* Find(uint64_t);
  static std::vector<ReachWebGLJournal*> Current();
  static void SetChangeCallback(void (*callback)(HTMLCanvasElement*));
  static void SetRetirementCallback(
      std::function<void()> (*callback)(HTMLCanvasElement*));
  void RefreshRetirementNotification();
  void ClearChangeNotification();
  void Resize(int, int) {}
  void Append(ReachWebGLCommand) {}
  void Fail(const char*) {}
  WeakPersistent<WebGLRenderingContextBase> context_;
  WeakPersistent<HTMLCanvasElement> element_;
  uint64_t id_;
  uint64_t generation_;
  struct Commands {
    Commands(std::atomic_size_t&, uint64_t) {}
  } commands_;
  size_t original_bytes_ = 0;
  bool change_pending_ = false;
  WeakPersistent<Document> retirement_document_;
  std::function<void()> retirement_notification_;
};

// NATIVE_JOURNAL_LIFETIME

}  // namespace blink

std::function<void(blink::WebLocalFrame*)>& WebGlChangeCallback() {
  static std::function<void(blink::WebLocalFrame*)> callback;
  return callback;
}
void OnFrameRequested(blink::LocalFrame&) {
  assert(false && "retirement must not request animation or capture");
}

// NATIVE_RETIREMENT_CALLBACKS

void RegisterCallbacks() {
  // NATIVE_CALLBACK_REGISTRATION
}

int main() {
  RegisterCallbacks();
  const auto runner = base::SingleThreadTaskRunner::GetCurrentDefault();
  int wakes = 0;
  blink::Document document;
  blink::LocalFrame frame;
  WebGlChangeCallback() = [&](blink::WebLocalFrame* web) {
    assert(!in_gc && web->frame == &frame);
    ++wakes;
  };
  document.frame = &frame;
  frame.document = &document;
  blink::HTMLCanvasElement first(document);
  blink::HTMLCanvasElement second(document);
  blink::WebGLRenderingContextBase context(first);
  blink::WebGLRenderingContextBase sibling(second);
  auto journal = std::make_unique<blink::ReachWebGLJournal>(context);
  context.journal = journal.get();
  auto other = std::make_unique<blink::ReachWebGLJournal>(sibling);
  sibling.journal = other.get();
  blink::Document hidden_document;
  blink::LocalFrame hidden_frame;
  hidden_document.frame = &hidden_frame;
  hidden_frame.document = &hidden_document;
  blink::HTMLCanvasElement hidden(hidden_document);
  blink::WebGLRenderingContextBase hidden_context(hidden);
  auto hidden_journal =
      std::make_unique<blink::ReachWebGLJournal>(hidden_context);
  hidden_context.journal = hidden_journal.get();
  // Removal's final discovery still includes the detached but uncollected
  // canvas.
  assert(blink::ReachWebGLJournal::Current().size() == 3);
#ifdef REACH_TEST_RETIREMENT_REBIND
  for (auto* current : blink::ReachWebGLJournal::Current())
    current->ClearChangeNotification();
#endif
  assert(wakes == 0 && runner->tasks.empty());
  // Weakness processing precedes finalization; neither finalizer may touch GC.
  in_gc = true;
  *context.weak = nullptr;
  *first.weak = nullptr;
  context.journal = nullptr;
  journal.reset();
  *sibling.weak = nullptr;
  *second.weak = nullptr;
  sibling.journal = nullptr;
  other.reset();
  in_gc = false;
  assert(wakes == 0);
  assert(runner->tasks.size() == 1);
  runner->RunUntilIdle();
  assert(wakes == 1 && "idle collection must independently wake discovery");
  assert(blink::ReachWebGLJournal::Current() ==
         std::vector<blink::ReachWebGLJournal*>{hidden_journal.get()});
  // A later retirement rearms once the first deferred notification was sent.
  *context.weak = &context;
  *first.weak = &first;
  journal = std::make_unique<blink::ReachWebGLJournal>(context);
  context.journal = journal.get();
  in_gc = true;
  *context.weak = nullptr;
  context.journal = nullptr;
  journal.reset();
  in_gc = false;
  runner->RunUntilIdle();
  assert(wakes == 2);
  // Child-document retirement wakes only its authoritative local root.
  blink::Document child_document;
  blink::LocalFrame child_frame;
  child_document.frame = &child_frame;
  child_frame.document = &child_document;
  child_frame.root = &frame;
  blink::HTMLCanvasElement child(child_document);
  blink::WebGLRenderingContextBase child_context(child);
  auto child_journal =
      std::make_unique<blink::ReachWebGLJournal>(child_context);
  child_context.journal = child_journal.get();
  in_gc = true;
  *child_context.weak = nullptr;
  child_context.journal = nullptr;
  child_journal.reset();
  in_gc = false;
  runner->RunUntilIdle();
  assert(wakes == 3);
  // A detached owning frame cannot send a stale root notification.
  *context.weak = &context;
  journal = std::make_unique<blink::ReachWebGLJournal>(context);
  context.journal = journal.get();
  in_gc = true;
  *context.weak = nullptr;
  context.journal = nullptr;
  journal.reset();
  in_gc = false;
  frame.attached = false;
  runner->RunUntilIdle();
  frame.attached = true;
  assert(wakes == 3);
  // An old document cannot notify a replacement at the same frame address.
  blink::Document replacement;
  replacement.frame = &frame;
  *context.weak = &context;
  journal = std::make_unique<blink::ReachWebGLJournal>(context);
  context.journal = journal.get();
  in_gc = true;
  *context.weak = nullptr;
  context.journal = nullptr;
  journal.reset();
  in_gc = false;
  frame.document = &replacement;
  runner->RunUntilIdle();
  assert(wakes == 3);
#ifdef REACH_TEST_RETIREMENT_REBIND
  // Authoritative discovery rebinds an adopted canvas to its new document.
  frame.document = &document;
  *context.weak = &context;
  journal = std::make_unique<blink::ReachWebGLJournal>(context);
  context.journal = journal.get();
  // A fresh adopting document allocates a supplement while discovery holds
  // only registry weak refs.
  blink::Document adopting_document;
  adopting_document.frame = &hidden_frame;
  hidden_frame.document = &adopting_document;
  first.document = &adopting_document;
  allocation_hook = [&] {
    assert(context.pins > 0 && first.pins > 0 && adopting_document.pins > 0);
  };
  journal->ClearChangeNotification();
  allocation_hook = {};
  assert(context.pins == 0 && first.pins == 0 && adopting_document.pins == 0);
  runner->RunUntilIdle();
  assert(wakes == 4);  // The old owner must rediscover its removed stream.
  int adopted_wakes = 0;
  WebGlChangeCallback() = [&](blink::WebLocalFrame* web) {
    assert(!in_gc && web->frame == &hidden_frame);
    ++adopted_wakes;
  };
  in_gc = true;
  *context.weak = nullptr;
  context.journal = nullptr;
  journal.reset();
  in_gc = false;
  runner->RunUntilIdle();
  assert(adopted_wakes == 1 && wakes == 4);
  first.document = &document;
  WebGlChangeCallback() = [&](blink::WebLocalFrame* web) {
    assert(!in_gc && web->frame == &frame);
    ++wakes;
  };
#endif
  // Pending tasks must not retain a document or dereference a cleared weak
  // root.
  frame.document = &document;
  *context.weak = &context;
  journal = std::make_unique<blink::ReachWebGLJournal>(context);
  context.journal = journal.get();
  in_gc = true;
  *context.weak = nullptr;
  context.journal = nullptr;
  journal.reset();
  *document.weak = nullptr;
  document.supplement.reset();
  in_gc = false;
  runner->RunUntilIdle();
  assert(wakes == 4);
  hidden_frame.attached = false;
  in_gc = true;
  *hidden_context.weak = nullptr;
  hidden_context.journal = nullptr;
  hidden_journal.reset();
  in_gc = false;
  runner->RunUntilIdle();
  assert(wakes == 4);
}
