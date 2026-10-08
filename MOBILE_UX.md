# Mobile Touchscreen UX Architecture & Design Guidelines

This document details the mobile touchscreen user experience (UX) architecture, touch-interaction patterns, and responsive design decisions implemented in **Robosoft-Robot-Control**.

---

## 1. Executive Summary & Design Principles

Controlling a physical robot through a mobile web browser presents unique UX challenges:
1. **Accidental Gesture Interference**: Mobile browsers default to gesture-heavy behaviors (pull-to-refresh, double-tap zoom, text selection loupes, context menus, and rubber-band overscroll) which disrupt real-time control.
2. **Horizontal Viewport Constraints**: Landscape orientation on mobile phones typically offers only **320px–390px of vertical height**, easily breaking standard modal windows and settings menus.
3. **Touch Sensitivity & Precision**: Sliders on touchscreens are prone to accidental jumps when touched near the track during scrolling.
4. **Platform Discrepancies**: iOS Safari and Android Chrome handle Fullscreen APIs, Bluetooth permissions, and system fonts differently.
5. **Session Continuity**: Robot control parameters (speed limits, steering trim, axis inversions) must persist reliably between browser reloads without requiring a backend server.

---

## 2. Touch Gesture Interference Prevention

Mobile browsers attempt to assist touch navigation with selection handles and contextual menus. In a gamepad-style interface, these behaviors cause severe control lag and accidental pauses.

### 2.1 Universal Text Selection & Callout Suppression
```css
*, *::before, *::after {
  box-sizing: border-box;
  -webkit-user-select: none;
  -moz-user-select: none;
  -ms-user-select: none;
  user-select: none;
  -webkit-touch-callout: none; /* Suppresses iOS link/image context magnifier */
}

::selection {
  background: transparent;
}
```

### 2.2 Event Guarding (DOM Level)
To prevent synthetic long-press context menus on Android Chrome and Safari when fingers remain pressed on buttons or labels:
```javascript
document.addEventListener("selectstart", (e) => {
  if (e.target.tagName !== "INPUT" && e.target.tagName !== "TEXTAREA") {
    e.preventDefault();
  }
});

document.addEventListener("contextmenu", (e) => {
  if (e.target.tagName !== "INPUT" && e.target.tagName !== "TEXTAREA") {
    e.preventDefault();
  }
});
```

### 2.3 Viewport & Overscroll Lock
```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
```
```css
body {
  overflow: hidden;
  overscroll-behavior: none; /* Disables elastic bounce and pull-to-refresh */
  touch-action: none;        /* Hands touch ownership completely to JS pointer events */
  -webkit-tap-highlight-color: transparent; /* Removes gray tap overlay */
}
```

---

## 3. Custom Drag-Only Slider Architecture

### 3.1 The "Track Jump" Problem
Native HTML `<input type="range">` elements jump the thumb to any clicked coordinate along the track. When users scroll down a settings modal on small touchscreens, brushing against a slider instantly alters robot parameters (e.g., unexpectedly dropping speed from 100% to 20%).

### 3.2 Solution: Thumb-Isolated Pointer Capture
The custom draggable slider component (`createCustomSlider`) strictly isolates pointer initiation to the thumb knob:

```html
<div class="custom-slider" id="speedSlider">
  <div class="slider-track"><div class="slider-fill"></div></div>
  <div class="slider-thumb"></div>
</div>
```

```css
.custom-slider {
  position: relative;
  width: calc(100% - 24px);
  margin: 10px 12px 14px 12px;
  height: 32px;
  display: flex;
  align-items: center;
  touch-action: pan-y; /* Allows modal vertical scrolling over track! */
}

.custom-slider .slider-thumb {
  position: absolute;
  top: 50%;
  left: 0%;
  width: 24px;
  height: 24px;
  background: #ffffff;
  border: 2px solid #2f80ff;
  border-radius: 50%;
  transform: translate(-50%, -50%);
  cursor: grab;
  touch-action: none;  /* Thumb locks out scroll while dragging */
}

/* Touch hit-area expansion without visual bulk */
.custom-slider .slider-thumb::before {
  content: "";
  position: absolute;
  top: -12px;
  bottom: -12px;
  left: -12px;
  right: -12px;
}
```

### 3.3 Drag Implementation Logic
- **`pointerdown`**: Registered **only** on `.slider-thumb`. Tapping the track does nothing.
- **`thumb.setPointerCapture(e.pointerId)`**: Keeps thumb tracking even if the user's finger accelerates outside the slider container.
- **Step Snapping & Float Rounding**: Automatically rounds to configured step intervals and eliminates floating-point precision errors with `+val.toFixed(4)`.

---

## 4. Landscape / Rotation Responsiveness

When rotated into landscape mode for dual-thumb driving, smartphone viewports experience severe vertical compression:

```
Portrait Viewport:   390px (W) x 844px (H) -> Plentiful vertical room
Landscape Viewport:  844px (W) x 390px (H) -> High vertical constraint
```

### 4.1 Modal Viewport Clamping
To prevent the settings modal buttons from being pushed off-screen:
```css
#settingsModal {
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  width: 340px;
  max-width: 92vw;
  max-height: 86vh;
  max-height: calc(100dvh - 28px); /* Accounts for mobile browser address bars */
  overflow-y: auto;
  -webkit-overflow-scrolling: touch;
  overscroll-behavior: contain;
}
```

### 4.2 Adaptive Landscape Media Query
Toggles compact spacing when viewport height is $\le 520\text{px}$:
```css
@media (max-height: 520px) {
  #settingsModal {
    max-height: calc(100dvh - 16px);
    padding: 12px 16px;
    width: 360px;
  }
  .settings-header { margin-bottom: 8px; font-size: 16px; }
  .settings-section { margin-bottom: 12px; }
  .toggle-row { padding: 4px 0; font-size: 14px; }
  .custom-slider { margin: 6px 12px 10px 12px; height: 28px; }
  .primary-btn, .secondary-btn, .danger-btn { padding: 8px; }
}
```

---

## 5. Bluetooth Connection Pill & Popover (OS-Style UX)

Traditional web interfaces hide BLE connectivity behind menus. This project consolidates connection status and control into a single **interactive Bluetooth status pill** inspired by mobile Control Centers.

### 5.1 States

| State | Visuals | Behavior on Tap |
| :--- | :--- | :--- |
| **Disconnected** | Red indicator dot, red border, text: `"Not connect"` | Directly calls `navigator.bluetooth.requestDevice()`. Satisfies browser user-gesture requirement with 0 extra taps. |
| **Connected** | Pulsing green dot, active green border, text: `device.name` (e.g. `ANT-BOT_09EA`) | Toggles a floating **Connection Options Popover** underneath the pill. |

### 5.2 Connection Options Popover
When connected, tapping the pill reveals a non-intrusive floating menu:
- Device Name & Connection State
- Real-time battery voltage and current consumption
- **`[Disconnect]`** (danger action)
- **`[Settings ⚙]`** (shortcut to full settings)
- Auto-dismisses when clicking outside.

### 5.3 Auto-Dismiss on Pairing
When the user connects via Settings or the Top Bar, the modal closes automatically upon GATT handshake completion, immediately dropping the driver into the active cockpit with a toast notification (`"Connected to ANT-BOT"`).

---

## 6. Fullscreen & Cross-Platform Icon Consistency

### 6.1 Fallback for iOS Safari
Apple disables the element `requestFullscreen()` API on iPhone browsers. The interface bridges this with an automated pseudo-fullscreen mode:
1. Body transitions to fixed viewport dimensions (`body.is-fullscreen`).
2. Window scroll offset resets (`window.scrollTo(0, 1)`).
3. Context toast prompts users: `"iOS: Tap Share > Add to Home Screen for borderless fullscreen"`.

### 6.2 Vector SVGs vs. Unicode Glyphs
- **Problem**: Unicode symbols like `🗗` (U+1F5D7) or `⛶` (U+26F6) are missing in Android and iOS default system fonts, rendering as an ugly missing glyph box with an "X" (tofu).
- **Solution**: Hand-coded inline SVGs (`maximize-2` and `minimize-2`) with `currentColor` stroke and `pointer-events: none` ensure crisp 60fps rendering across all mobile DPIs.

---

## 7. Client-Side State Persistence

Robot preferences are stored in the browser's `localStorage` to ensure instant consistency across sessions without requiring server authentication.

### 7.1 Persisted Schema
```json
{
  "maxSpeed": 1.0,
  "steeringGain": 1.0,
  "invertSideway": false,
  "invertYaw": false,
  "invertForward": false,
  "cameraBG": false,
  "tone": "dark",
  "theme": "blue"
}
```

### 7.2 Safe Schema Hydration
```javascript
let appSettings = Object.assign({
  maxSpeed: 1.0,
  steeringGain: 1.0,
  invertSideway: false,
  invertYaw: false,
  invertForward: false,
  cameraBG: false,
  tone: "dark",
  theme: "blue"
}, JSON.parse(localStorage.getItem("settings") || "{}"));
```

### 7.3 Cache & PWA Invalidation
To prevent mobile devices from holding onto stale cached scripts when new versions are released, `APP_VERSION` is validated on boot:
```javascript
const APP_VERSION = "1.0.8";
if (localStorage.getItem("app_version") !== APP_VERSION) {
  localStorage.setItem("app_version", APP_VERSION);
  if ("serviceWorker" in navigator) {
    navigator.serviceWorker.getRegistrations().then(regs => {
      regs.forEach(r => r.unregister());
    });
  }
}
```

### 7.4 Separated Control & Personalization Architecture
To maintain a focused cockpit experience on mobile screens, settings are segregated into two distinct domains:
- **Controls Domain (`#panelControls`)**: Driving kinematics parameters (Max Speed default 100%, Steering Gain default 1.00x, inversions, reset defaults), BLE connection actions, and camera feed toggle.
- **Personalization Domain (`#panelPersonalize`)**:
  - **Tone Control**: Managed via `<html data-tone="dark|light">`. Dark tone preserves cockpit night vision; light tone adapts for high ambient daylight use.
  - **Accent Themes**: Managed via `<html data-theme="blue|green|purple|amber|crimson|cyan">`. Dynamic CSS variables (`--accent`, `--accent-hover`, `--accent-glow`, `--accent-badge-bg`, `--accent-badge-text`) recalculate across buttons, sliders, badges, and indicators.
  - **Seamless Modal Navigation**: Switch instantaneously between Controls and Personalize tabs in the settings panel with zero screen clutter.

---

## 8. Dual Virtual Joystick Kinematics & Safety Lockout

### 8.1 BLE Connection Safety Lockout
- When Bluetooth is disconnected, joysticks are automatically given the `.disabled` class:
  - Visual opacity drops to 32% with grayscale filtering.
  - Pointer interaction is locked via `pointer-events: none` and JavaScript guards.
  - All kinematics registers (`forward`, `sideway`, `yaw`, `leftForward`, `rightJoyActive`) are immediately zeroed and knobs are re-centered to `(0, 0)` (`translate(-50%, -50%)`).
- Upon successful BLE GATT handshake, joysticks smoothly transition into active, touch-responsive mode.

### 8.2 Relative Joystick Touch Handling
- Left Joystick: Steers yaw (rotation around center) + forward/reverse backup.
- Right Joystick: 2D planar motion (sideway strafe + forward/reverse). Takes active drive precedence when touched.
- Uses `pointerdown`, `pointermove`, and `pointerup` with `setPointerCapture` to prevent touch dropouts when thumb crosses outside the circular joystick boundary.
- Deadband & circular bounding:
  $$\text{dist} = \min(R_{\max}, \sqrt{\Delta x^2 + \Delta y^2})$$
  $$\text{angle} = \text{atan2}(\Delta y, \Delta x)$$
