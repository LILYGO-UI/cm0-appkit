#pragma once

#include <cctype>
#include <string>

namespace lilygo_input_detail {

// Keep evdev discovery and the sysfs status indicator on one predicate. These
// stable Linux input key codes are private so the status reader and its host
// tests do not need Linux headers.
template <typename KeyPresent>
bool is_tca8418_keyboard(const char *name, KeyPresent has_key)
{
    if (!name) return false;
    std::string folded(name);
    for (char &character : folded)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if (folded.find("tca8418") == std::string::npos) return false;

    // Enter, Escape, Tab, Backspace, arrows, A, Z, 0, 9, and Space identify the
    // supported navigation/text keypad. Its shipped matrix has no Delete,
    // Home, or End keys; decoding those optional keys must not gate discovery.
    constexpr unsigned int required[] = {
        28, 1, 15, 14, 103, 108, 105, 106, 30, 44, 11, 10, 57,
    };
    for (const unsigned int key : required)
        if (!has_key(key)) return false;
    return true;
}

}  // namespace lilygo_input_detail
