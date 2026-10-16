// SimpleMessage — InputText 与 std::string 的胶水实现
#include "host/text_input.h"

#include <cstdio>

#include "imgui.h"

namespace sm {
namespace host {

bool InputTextStdString(const char* id, const char* key, std::string* value,
                        std::size_t capacity, TextInputState* state) {
  if (value == nullptr) return false;
  if (capacity == 0) capacity = 1;

  if (state == nullptr) {
    ImGui::TextUnformatted(value->c_str());
    return false;
  }

  const bool same_field = (state->key == key);
  if (!same_field || !state->editing) {
    state->key = key;
    state->buffer.assign(capacity + 1, '\0');
    std::snprintf(state->buffer.data(), state->buffer.size(), "%s", value->c_str());
  }

  const bool changed = ImGui::InputText(id, state->buffer.data(), state->buffer.size());
  state->editing = ImGui::IsItemActive();
  if (changed) value->assign(state->buffer.data());
  return changed;
}

}  // namespace host
}  // namespace sm
