/**
 * Keyboard & DirectInput scancode helpers for Magelight/Ultralight views.
 * Handles Ultralight 2.0 numpad quirks where NumpadEnter or empty `e.code`
 * can be emitted for numpad keys.
 */

const NUMPAD_CODE_TO_VK: Record<string, number> = {
  Numpad0: 96,
  Numpad1: 97,
  Numpad2: 98,
  Numpad3: 99,
  Numpad4: 100,
  Numpad5: 101,
  Numpad6: 102,
  Numpad7: 103,
  Numpad8: 104,
  Numpad9: 105,
  NumpadMultiply: 106,
  NumpadAdd: 107,
  NumpadSubtract: 109,
  NumpadDecimal: 110,
  NumpadDivide: 111,
  NumpadEnter: 13,
}

const DOM_CODE_TO_DX_SCANCODE: Record<string, number> = {
  Escape: 0x01,
  Digit1: 0x02,
  Digit2: 0x03,
  Digit3: 0x04,
  Digit4: 0x05,
  Digit5: 0x06,
  Digit6: 0x07,
  Digit7: 0x08,
  Digit8: 0x09,
  Digit9: 0x0a,
  Digit0: 0x0b,
  Minus: 0x0c,
  Equal: 0x0d,
  Backspace: 0x0e,
  Tab: 0x0f,
  KeyQ: 0x10,
  KeyW: 0x11,
  KeyE: 0x12,
  KeyR: 0x13,
  KeyT: 0x14,
  KeyY: 0x15,
  KeyU: 0x16,
  KeyI: 0x17,
  KeyO: 0x18,
  KeyP: 0x19,
  BracketLeft: 0x1a,
  BracketRight: 0x1b,
  Enter: 0x1c,
  ControlLeft: 0x1d,
  KeyA: 0x1e,
  KeyS: 0x1f,
  KeyD: 0x20,
  KeyF: 0x21,
  KeyG: 0x22,
  KeyH: 0x23,
  KeyJ: 0x24,
  KeyK: 0x25,
  KeyL: 0x26,
  Semicolon: 0x27,
  Quote: 0x28,
  Backquote: 0x29,
  ShiftLeft: 0x2a,
  Backslash: 0x2b,
  KeyZ: 0x2c,
  KeyX: 0x2d,
  KeyC: 0x2e,
  KeyV: 0x2f,
  KeyB: 0x30,
  KeyN: 0x31,
  KeyM: 0x32,
  Comma: 0x33,
  Period: 0x34,
  Slash: 0x35,
  ShiftRight: 0x36,
  NumpadMultiply: 0x37,
  AltLeft: 0x38,
  Space: 0x39,
  CapsLock: 0x3a,
  F1: 0x3b,
  F2: 0x3c,
  F3: 0x3d,
  F4: 0x3e,
  F5: 0x3f,
  F6: 0x40,
  F7: 0x41,
  F8: 0x42,
  F9: 0x43,
  F10: 0x44,
  NumLock: 0x45,
  ScrollLock: 0x46,
  Numpad7: 0x47,
  Numpad8: 0x48,
  Numpad9: 0x49,
  NumpadSubtract: 0x4a,
  Numpad4: 0x4b,
  Numpad5: 0x4c,
  Numpad6: 0x4d,
  NumpadAdd: 0x4e,
  Numpad1: 0x4f,
  Numpad2: 0x50,
  Numpad3: 0x51,
  Numpad0: 0x52,
  NumpadDecimal: 0x53,
  F11: 0x57,
  F12: 0x58,
  NumpadEnter: 0x9c,
  ControlRight: 0x9d,
  NumpadDivide: 0xb5,
  AltRight: 0xb8,
  Pause: 0xc5,
  Home: 0xc7,
  ArrowUp: 0xc8,
  PageUp: 0xc9,
  ArrowLeft: 0xcb,
  ArrowRight: 0xcd,
  End: 0xcf,
  ArrowDown: 0xd0,
  PageDown: 0xd1,
  Insert: 0xd2,
  Delete: 0xd3,
}

const VK_TO_DX_SCANCODE: Record<number, number> = {
  8: 0x0e, // Backspace
  9: 0x0f, // Tab
  13: 0x1c, // Enter
  16: 0x2a, // Shift
  17: 0x1d, // Ctrl
  18: 0x38, // Alt
  19: 0xc5, // Pause
  20: 0x3a, // CapsLock
  27: 0x01, // Escape
  32: 0x39, // Space
  33: 0xc9, // PageUp
  34: 0xd1, // PageDown
  35: 0xcf, // End
  36: 0xc7, // Home
  37: 0xcb, // ArrowLeft
  38: 0xc8, // ArrowUp
  39: 0xcd, // ArrowRight
  40: 0xd0, // ArrowDown
  45: 0xd2, // Insert
  46: 0xd3, // Delete
  48: 0x0b, // 0
  49: 0x02, // 1
  50: 0x03, // 2
  51: 0x04, // 3
  52: 0x05, // 4
  53: 0x06, // 5
  54: 0x07, // 6
  55: 0x08, // 7
  56: 0x09, // 8
  57: 0x0a, // 9
  65: 0x1e, // A
  66: 0x30, // B
  67: 0x2e, // C
  68: 0x20, // D
  69: 0x12, // E
  70: 0x21, // F
  71: 0x22, // G
  72: 0x23, // H
  73: 0x17, // I
  74: 0x24, // J
  75: 0x25, // K
  76: 0x26, // L
  77: 0x32, // M
  78: 0x31, // N
  79: 0x18, // O
  80: 0x19, // P
  81: 0x10, // Q
  82: 0x13, // R
  83: 0x1f, // S
  84: 0x14, // T
  85: 0x16, // U
  86: 0x2f, // V
  87: 0x11, // W
  88: 0x2d, // X
  89: 0x15, // Y
  90: 0x2c, // Z
  96: 0x52, // Numpad0
  97: 0x4f, // Numpad1
  98: 0x50, // Numpad2
  99: 0x51, // Numpad3
  100: 0x4b, // Numpad4
  101: 0x4c, // Numpad5
  102: 0x4d, // Numpad6
  103: 0x47, // Numpad7
  104: 0x48, // Numpad8
  105: 0x49, // Numpad9
  106: 0x37, // NumpadMultiply
  107: 0x4e, // NumpadAdd
  109: 0x4a, // NumpadSubtract
  110: 0x53, // NumpadDecimal
  111: 0xb5, // NumpadDivide
  112: 0x3b, // F1
  113: 0x3c, // F2
  114: 0x3d, // F3
  115: 0x3e, // F4
  116: 0x3f, // F5
  117: 0x40, // F6
  118: 0x41, // F7
  119: 0x42, // F8
  120: 0x43, // F9
  121: 0x44, // F10
  122: 0x57, // F11
  123: 0x58, // F12
  144: 0x45, // NumLock
  145: 0x46, // ScrollLock
  186: 0x27, // ;
  187: 0x0d, // =
  188: 0x33, // ,
  189: 0x0c, // -
  190: 0x34, // .
  191: 0x35, // /
  192: 0x29, // `
  219: 0x1a, // [
  220: 0x2b, // \
  221: 0x1b, // ]
  222: 0x28, // '
}

export function getNormalizedKeyCode(
  e: Pick<KeyboardEvent, 'keyCode' | 'which' | 'code' | 'key' | 'location'>,
): number {
  if (e.code === 'NumpadEnter' || (e.key === 'Enter' && e.location === 3)) {
    return 13
  }
  if (e.code && NUMPAD_CODE_TO_VK[e.code] !== undefined) {
    return NUMPAD_CODE_TO_VK[e.code]
  }
  return e.keyCode || e.which || 0
}

export function isEnterKey(e: KeyboardEvent): boolean {
  return e.key === 'Enter' || e.code === 'NumpadEnter' || getNormalizedKeyCode(e) === 13
}

export function isEscapeKey(e: KeyboardEvent): boolean {
  return e.key === 'Escape' || e.code === 'Escape' || getNormalizedKeyCode(e) === 27
}

export function convertKeyboardEventToScanCode(e: KeyboardEvent): number {
  if (e.code && DOM_CODE_TO_DX_SCANCODE[e.code] !== undefined) {
    return DOM_CODE_TO_DX_SCANCODE[e.code]
  }
  const keyCode = getNormalizedKeyCode(e)
  if (keyCode === 13 && e.location === 3) {
    return 0x9c // NumpadEnter
  }
  return VK_TO_DX_SCANCODE[keyCode] ?? 0
}

export function isEditableTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) {
    return false
  }
  const tag = target.tagName
  if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') {
    return true
  }
  return target.isContentEditable || target.closest('[contenteditable="true"]') !== null
}
