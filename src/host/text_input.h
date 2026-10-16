// SimpleMessage — InputText 与 std::string 之间的那一小段胶水
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sm {
namespace host {

struct TextInputState {
  std::string       key;
  std::vector<char> buffer;
  bool              editing = false;
};

bool InputTextStdString(const char* id, const char* key, std::string* value,
                        std::size_t capacity, TextInputState* state);

}  // namespace host
}  // namespace sm
