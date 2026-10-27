# Keyboard input contract

This document is the compatibility contract between the Linux CM0 image,
LilyGoUI AppKit, and applications using the detachable TCA8418 keypad. It also
defines the runtime behavior that applications can rely on for physical
keyboard input, the SDL simulator keyboard, and focus navigation.

The contract is versioned with AppKit. Applications must update their pinned
`third_party/cm0-appkit` revision before relying on a new rule. AppKit and LVGL
are statically linked into each application, so changing an installed SDK does
not change already-installed application executables; applications must be
rebuilt after updating their pinned source SDK.

## Device boundary and discovery

The Linux `tca8418_keypad` driver owns matrix scanning and the device-tree
`linux,keymap`. It emits normal Linux `EV_KEY` events. AppKit is the only layer
that discovers the event node and converts those events to LVGL keys.

Applications must not read TCA8418 registers, inspect the matrix, open an
`event*` node, install a second evdev reader, or replace the AppKit LVGL
keyboard patch. A diagnostic page may display evdev state, but it must not feed
that state into application controls or implement a second primary keyboard
path.

On Linux, AppKit accepts an event device only when both conditions hold:

1. `EVIOCGNAME` contains `tca8418`, case-insensitively.
2. The `EV_KEY` capability bitmap contains Enter, Escape, Tab, Backspace,
   Up, Down, Left, Right, Space, and the representative text keys A, Z, 0, and 9.

Delete, Home, and End are optional: the shipped T-DisplayCM0 device-tree
matrix has none of these keys. AppKit maps them when a keymap provides them,
but their absence must not reject the entire keyboard. The discovery check is
a device capability guard, not a requirement to implement every decoded key.

The same predicate is used for startup detection, hotplug reconnect, the
hardware-keyboard status flag, and software-keyboard suppression. The SDL
simulator provides a compatible host-keyboard path without the Linux name
check.

## LVGL mapping and interaction

AppKit's patched evdev driver maps navigation and editing keys to the LVGL
`LV_KEY_*` constants. Printable letters, digits, space, and standard US
punctuation keys are delivered as characters. Shift and Caps Lock affect
letters and punctuation. Ctrl, Alt, Meta, and Fn are consumed by the driver
until a modifier contract is added to the public API.

Applications must use `cm0_app_run` and consume the default
`LilyGoUI::Runtime` input group. AppKit owns keyboard creation, hotplug,
key-repeat filtering, focus collection, focus rings, modal focus scopes, and
software-keyboard suppression. Applications may register business-level
`LV_EVENT_KEY` handlers for their focused controls.

- Tab and Shift+Tab move forward and backward through visible, enabled
  controls and marked browsing regions. Focus navigation scrolls the focused
  control into view.
- Applications may mark an overflowing scrollable content area with
  `cm0_input_set_scroll_region()`. Arrows enter the area, navigate its visible
  controls and scroll incrementally through read-only contents. Scroll steps
  are limited to a quarter of the visible viewport; focus never jumps directly
  to an offscreen control. At a scroll boundary, or on an axis the area does
  not scroll, arrows can leave for an adjacent visible control.
- Arrow keys choose the geometrically nearest control in the requested
  direction, preferring the same row or column. Explicit browsing regions
  restrict candidates to visible controls and scroll before focus moves
  outside them in the scrolling direction. Unmarked lists retain automatic
  reveal-on-focus navigation to offscreen controls. A focused textarea,
  slider, dropdown, or button matrix retains its native arrow handling.
  Switches use all four arrows for navigation; Enter or Space
  toggles the value once per press. Other checkable controls retain Left/Right
  adjustment and Enter activation, while Up/Down navigate. An explicit
  application key handler retains ownership of its control.
- Enter activates buttons and clickable cards. It submits an active one-line
  textarea and emits `LV_EVENT_READY`; applications should connect that event
  to their submit command.
- A focused textarea receives letters, numbers, punctuation, and editing keys
  directly.
- Escape cancels an active editor and restores the enclosing focus scope.
  Escape from an ordinary control exits the application.
- Existing software-keyboard `READY` and `CANCEL` callbacks continue to work.
  Software keyboards are suppressed while a physical keyboard is connected.

Normal LVGL group widgets are discovered automatically. Custom clickable
objects with `CLICKED` or `SHORT_CLICKED` callbacks also participate;
decorative containers do not. Applications can explicitly mark overflowing
scrollable content areas for keyboard browsing. Hidden and disabled ancestor
subtrees are excluded. Controls created dynamically receive the same behavior
during runtime housekeeping. Tab leaves native controls whose own key handling
is active.

## Runtime takeover and modal views

An application that temporarily takes exclusive keypad input, such as a game
or terminal, must save and restore the current LVGL group, ignore stale key-up
events after detaching, and restore the group from every normal-exit, cancel,
and input-device-delete path. The takeover must not replace AppKit evdev
decoding or the global Escape and Enter rules; those rules must remain in force
when the takeover ends.

When creating a modal overlay or a full-screen view that covers other
controls, call `cm0_input_set_focus_scope(object)` from `<cm0/input.h>`. The
visible frontmost marked scope owns keyboard navigation. Hiding or deleting it
restores navigation in the enclosing scope, and nested scopes are supported.
Each scope should contain an accessible cancel or back control. The marker is
owned by the LVGL object and needs no separate cleanup.

By default, the runtime draws a three-pixel focus ring inside the focused
control, after its children: a two-pixel dark neutral outer edge and a
one-pixel white inner edge. This keeps focus visible on blue action fills,
selected states, switch indicators, and dark content. The ring does not change layout, and
photo or canvas children cannot cover it. There is no outside outline to be
clipped at a list or grid boundary. The ring stays hidden until the user
presses a key or touches a control, and it is also hidden when the keyboard is
absent.

A control can use a native LVGL focus outline by explicitly setting a positive
local `outline_width` for `LV_STATE_FOCUS_KEY` or `LV_STATE_FOCUSED` before
runtime housekeeping discovers it. AppKit preserves that control's outline
opacity and skips its inner ring. Configure the outline's padding, color, and
opacity with the same selector; theme-inherited widths alone do not opt in.
The application must leave enough space in ancestor containers for the outline
width plus padding. Native outlines follow their LVGL state selectors, including
keyboard-only visibility when configured for `LV_STATE_FOCUS_KEY`.

## Compatibility, verification, and rollout

An application is compatible only when its embedded AppKit contains
`include/cm0/input.h`, `src/input.cpp`, `src/runtime.cpp`, the patched evdev
keyboard file, the runtime input tests, and this contract, and when its
application sources do not implement a second primary evdev path. Run
AppKit's `tools/check_appkit_compatibility.py` before committing an application
gitlink.

The `lilygo-ui-runtime-input-*` tests cover hotplug, real keypad events, directional
navigation, editing, disabled and hidden pages, modal nesting and deletion,
existing key handlers, idle scroll stability, and software-keyboard isolation.
Focus-ring rendering checks every pixel along all four sides over an opaque
child in a zero-padding clipped parent at 320x568, 568x1232, 1232x568, and
1024x768. It also verifies that explicit native outlines survive housekeeping
and keypad navigation, render outside the control without a second inner ring,
and disappear when focus moves away.

The host test suite does not imply device installation or a hardware TCA8418
session. Publish the AppKit revision and pin it in every application repository
before releasing rebuilt application packages.
