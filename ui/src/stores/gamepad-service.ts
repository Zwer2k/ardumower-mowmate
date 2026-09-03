// Globaler Gamepad-Service: Läuft unabhängig vom RC-Dialog und aktualisiert den gamepadStore.
import { browser } from '$app/environment';
import { gamepadStore, type GamepadState } from './gamepad';

const DEADZONE = 0.12;
const SEND_INTERVAL = 200;

let connected = false;
let gamepadIndex: number | null = null;
let linearSpeed = 0;
let angularSpeed = 0;
let rawX = 0;
let rawY = 0;
let paused = false;
let rafId: number | null = null;
let lastButtonState = new Map<number, boolean>();
let lastSendTime = 0;

function updateStore() {
  gamepadStore.set({
    connected,
    index: gamepadIndex,
    linear: linearSpeed,
    angular: angularSpeed,
    rawX,
    rawY,
    paused,
    lastButtonPress: Object.fromEntries(lastButtonState),
  });
}

function onGamepadConnected(e: GamepadEvent) {
  const gp = e.gamepad;
  connected = true;
  gamepadIndex = gp.index;
  updateStore();
  startLoop();
}

function onGamepadDisconnected(e: GamepadEvent) {
  if (e.gamepad.index === gamepadIndex) {
    connected = false;
    gamepadIndex = null;
    paused = false;
    linearSpeed = 0;
    angularSpeed = 0;
    updateStore();
    stopLoop();
  }
}

function startLoop() {
  if (rafId) return;
  loop();
}

function stopLoop() {
  if (rafId) {
    cancelAnimationFrame(rafId);
    rafId = null;
  }
}

function loop() {
  rafId = requestAnimationFrame(loop);

  if (gamepadIndex !== null) {
    const gp = navigator.getGamepads()[gamepadIndex];
    if (!gp) {
      connected = false;
      gamepadIndex = null;
      linearSpeed = 0;
      angularSpeed = 0;
      updateStore();
      return;
    }
    readSticks(gp);
    readButtons(gp);
    return;
  }

  // Scan for newly active gamepads
  if (!connected) {
    const pads = navigator.getGamepads();
    for (let i = 0; i < pads.length; i++) {
      const gp = pads[i];
      if (gp) {
        let active = false;
        for (const btn of gp.buttons) {
          if (btn.pressed) { active = true; break; }
        }
        if (!active) {
          for (const axis of gp.axes) {
            if (Math.abs(axis) > 0.5) { active = true; break; }
          }
        }
        if (active) {
          connected = true;
          gamepadIndex = gp.index;
          paused = false;
          lastButtonState.clear();
          updateStore();
        }
      }
    }
  }
}

function readSticks(gp: Gamepad) {
  let nx0 = gp.axes[0] ?? 0;
  let ny0 = gp.axes[1] ?? 0;
  let sensitivity = 0.5;

  const leftMag = Math.sqrt(nx0 * nx0 + ny0 * ny0);
  if (leftMag < DEADZONE && gp.axes.length >= 4) {
    nx0 = gp.axes[2] ?? 0;
    ny0 = gp.axes[3] ?? 0;
    sensitivity = 0.25;
  }

  const mag0 = Math.sqrt(nx0 * nx0 + ny0 * ny0);
  if (mag0 < DEADZONE) {
    if (linearSpeed !== 0 || angularSpeed !== 0 || rawX !== 0 || rawY !== 0) {
      linearSpeed = 0;
      angularSpeed = 0;
      rawX = 0;
      rawY = 0;
      updateStore();
    }
    return;
  }

  // Raw axes for map editing (before any transformation)
  rawX = nx0;
  rawY = ny0;

  const scale = (mag0 - DEADZONE) / (1 - DEADZONE) / mag0;
  let nx = nx0 * scale;
  let ny = ny0 * scale;

  const mag = Math.sqrt(nx * nx + ny * ny);
  const curvedMag = mag * mag;
  const curveScale = curvedMag / mag;
  nx *= curveScale;
  ny *= curveScale;

  nx *= sensitivity;
  ny *= sensitivity;

  let lin = Math.round(-ny * 0.5 * 100) / 100;
  let ang = Math.round(-nx * 0.5 * 100) / 100;
  if (lin < 0) ang = -ang;
  if (Math.abs(lin) < 0.005) lin = 0;
  if (Math.abs(ang) < 0.005) ang = 0;

  linearSpeed = lin;
  angularSpeed = ang;
  updateStore();
}

function readButtons(gp: Gamepad) {
  const now = Date.now();
  const buttons = gp.buttons;
  if (!buttons.length) return;

  // PS button (index 16) = pause/resume
  if (buttons.length > 16) {
    const psPressed = buttons[16].pressed;
    const wasPsPressed = lastButtonState.get(16) ?? false;
    if (psPressed && !wasPsPressed) {
      paused = !paused;
      updateStore();
    }
    lastButtonState.set(16, psPressed);
  }

  if (paused) return;

  // Throttle button updates
  if (now - lastSendTime < SEND_INTERVAL) return;

  for (let i = 0; i < Math.min(buttons.length, 4); i++) {
    const pressed = buttons[i].pressed;
    const wasPressed = lastButtonState.get(i) ?? false;

    if (pressed && !wasPressed) {
      lastButtonState.set(i, now);
      updateStore();
      lastSendTime = now;
    } else if (!pressed && wasPressed) {
      lastButtonState.set(i, 0);
    }
  }
}

// Initialize only in browser
if (browser) {
  window.addEventListener('gamepadconnected', onGamepadConnected);
  window.addEventListener('gamepaddisconnected', onGamepadDisconnected);

  // Check for already-connected gamepads
  const existing = navigator.getGamepads();
  for (let i = 0; i < existing.length; i++) {
    const gp = existing[i];
    if (gp) {
      connected = true;
      gamepadIndex = gp.index;
      updateStore();
      startLoop();
      break;
    }
  }
}
