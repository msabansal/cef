#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLenum = unsigned int;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_ACTIVE_ATTRIBUTES = 0x8B89;
constexpr GLenum GL_ACTIVE_ATTRIBUTE_MAX_LENGTH = 0x8B8A;
constexpr GLenum GL_ACTIVE_UNIFORM_BLOCKS = 0x8A36;
constexpr GLenum GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH = 0x8A35;

void RequireDestination(bool valid) {
  if (!valid)
    throw std::runtime_error(
        "GetProgramiv destination must be initialized to 0 or -1");
}
#define GPU_CLIENT_VALIDATE_DESTINATION_INITALIZATION_ASSERT(v) \
  RequireDestination(v)
// PINNED_DESTINATION_GUARD

namespace base {
struct DictValue {
  std::map<std::string, int> integers;
  std::map<std::string, DictValue> dictionaries;
  void Set(std::string key, int value) { integers[std::move(key)] = value; }
  void Set(std::string key, DictValue value) {
    dictionaries[std::move(key)] = std::move(value);
  }
};
}  // namespace base

// Only the GLES/Chromium boundaries are substituted. The initialization
// contract below and the complete journal method come from pinned source.
struct GL {
  GLint linked = 1;
  GLint attributes = 2;
  GLint attribute_length = 9;
  GLint blocks = 1;
  GLint block_length = 12;
  bool lose_on_block_query = false;
  std::vector<GLenum> queries;
  void GetProgramiv(GLuint program, GLenum pname, GLint* params) {
    // PINNED_PROGRAM_QUERY_VALIDATION
    assert(program == 17);
    queries.push_back(pname);
    if (pname == GL_LINK_STATUS)
      *params = linked;
    else if (pname == GL_ACTIVE_ATTRIBUTES)
      *params = attributes;
    else if (pname == GL_ACTIVE_ATTRIBUTE_MAX_LENGTH)
      *params = attribute_length;
    else if (pname == GL_ACTIVE_UNIFORM_BLOCKS && !lose_on_block_query)
      *params = blocks;
    else if (pname == GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH &&
             !lose_on_block_query)
      *params = block_length;
    else
      assert(lose_on_block_query);
  }
  void GetActiveAttrib(GLuint program, GLuint index, GLsizei capacity,
                       GLsizei* length, GLint* size, GLenum* type, char* name) {
    assert(program == 17 && index < 2 && capacity >= 9);
    const char* text = index == 0 ? "position" : "color";
    *length = static_cast<GLsizei>(std::strlen(text));
    *size = 1;
    *type = 0x1406;
    std::memcpy(name, text, static_cast<size_t>(*length) + 1);
  }
  GLint GetAttribLocation(GLuint program, const char* name) {
    assert(program == 17);
    return std::string(name) == "position" ? 3 : 7;
  }
  void GetActiveUniformBlockName(GLuint program, GLuint index, GLsizei capacity,
                                 GLsizei* length, char* name) {
    assert(program == 17 && index == 0 && capacity >= 12);
    *length = 11;
    std::memcpy(name, "FrameParams", 12);
  }
};
struct WebGLProgram {
  GLuint Object() const { return 17; }
};
struct Context {
  GL gl;
  bool webgl2 = true;
  GL* ContextGL() { return &gl; }
  bool IsWebGL2() const { return webgl2; }
};
struct ReachWebGLCommand {
  base::DictValue value;
  std::string error;
};
struct ReachWebGLJournal {
  Context* context_;
  void Program(ReachWebGLCommand&, WebGLProgram*);
};

// NATIVE_PROGRAM_METHOD

int main() {
  try {
    WebGLProgram program;
    Context context;
    ReachWebGLJournal journal{&context};
    ReachWebGLCommand command;
    journal.Program(command, &program);
    assert(command.error.empty());
    assert(command.value.integers.at("value") == 1);
    const auto& attributes = command.value.dictionaries.at("attributes");
    assert(attributes.integers.at("position") == 3);
    assert(attributes.integers.at("color") == 7);
    assert(command.value.dictionaries.at("uniformBlocks")
               .integers.at("FrameParams") == 0);

    context.gl.lose_on_block_query = true;
    command = {};
    journal.Program(command, &program);
    assert(command.error.empty());
    assert(command.value.dictionaries.at("uniformBlocks").integers.empty());

    context.gl.lose_on_block_query = false;
    context.webgl2 = false;
    context.gl.queries.clear();
    command = {};
    journal.Program(command, &program);
    assert(command.error.empty() && context.gl.queries.size() == 3);
    assert(command.value.dictionaries.at("attributes")
               .integers.at("position") == 3);
    assert(!command.value.dictionaries.contains("uniformBlocks"));

    context.webgl2 = true;
    context.gl.attributes = 33;
    command = {};
    journal.Program(command, &program);
    assert(!command.error.empty());
    context.gl.attributes = 2;
    context.gl.blocks = 257;
    command = {};
    journal.Program(command, &program);
    assert(!command.error.empty());

    context.gl.linked = 0;
    context.gl.queries.clear();
    command = {};
    journal.Program(command, &program);
    assert(command.error.empty() && context.gl.queries.size() == 1);
    assert(command.value.integers.at("value") == 0);
    std::cout << "Program capture: initialized GLES destinations, attributes, "
                 "uniform blocks, lost-context outputs and bounds PASS\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
