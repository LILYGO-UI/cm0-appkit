#include "../src/keyboard_device.h"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    // T-DisplayCM0 tca8418 event1, captured from the target on 2026-10-15:
    // 40 0 10000 0 0 0 0 0 200016800080001f ff07f47ff3ffcffe
    // This is the actual matrix capability set, including Fn and voice keys,
    // rather than an idealized full desktop keyboard.
    std::array<uint64_t, 10> bits = {
        UINT64_C(0xff07f47ff3ffcffe), UINT64_C(0x200016800080001f),
        0, 0, 0, 0, 0, UINT64_C(0x10000), 0, UINT64_C(0x40),
    };
    auto has_key = [&](unsigned int key) {
        return key / 64 < bits.size() &&
               (bits[key / 64] & (UINT64_C(1) << (key % 64))) != 0;
    };
    auto detected = [&](const char *name) {
        return lilygo_input_detail::is_tca8418_keyboard(name, has_key);
    };

    assert(!has_key(102));  // Home
    assert(!has_key(107));  // End
    assert(!has_key(111));  // Delete
    assert(detected("tca8418"));
    assert(detected("TCA8418 Keypad"));
    assert(detected("tca8418_keypad"));
    assert(!detected("USB Keyboard"));
    assert(!detected("gpio-keys"));
    assert(!detected(""));
    assert(!detected(nullptr));

    // Adding optional keys is compatible, but losing the core text or
    // navigation keys must still reject an incomplete device.
    for (const unsigned int key : {102U, 107U, 111U}) {
        bits[key / 64] |= UINT64_C(1) << (key % 64);
        assert(detected("tca8418"));
    }
    for (const unsigned int key :
         {28U, 1U, 15U, 14U, 103U, 108U, 105U, 106U, 30U, 44U, 11U, 10U, 57U}) {
        assert(has_key(key));
        bits[key / 64] &= ~(UINT64_C(1) << (key % 64));
        assert(!detected("tca8418"));
        bits[key / 64] |= UINT64_C(1) << (key % 64);
    }
    bits.fill(0);
    assert(!detected("tca8418"));
}
