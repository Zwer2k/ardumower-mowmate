import { readable } from "svelte/store";
import { browser } from "$app/environment";

export type PointerKind = "mouse" | "touch" | "pen";

/**
 * Kind of the pointer the user is currently working with.
 *
 * Hybrid devices (laptops with touchscreen, tablets with mouse) report touch
 * capability although the user may be operating a mouse. Deciding the hit
 * area size once per page load from `maxTouchPoints` therefore enlarges the
 * hit areas for mouse users as well. This store follows the last pointer
 * event instead, so a finger gets the enlarged hit area and a mouse or pen
 * gets the exact one.
 */
export const activePointerKind = readable<PointerKind>(
  browser && window.matchMedia("(pointer: coarse)").matches ? "touch" : "mouse",
  (set) => {
    if (!browser) return;
    let current: PointerKind | null = null;
    const update = (event: PointerEvent) => {
      const kind: PointerKind =
        event.pointerType === "touch" ? "touch" : event.pointerType === "pen" ? "pen" : "mouse";
      if (kind !== current) {
        current = kind;
        set(kind);
      }
    };
    const opts: AddEventListenerOptions = { capture: true, passive: true };
    window.addEventListener("pointerdown", update, opts);
    window.addEventListener("pointermove", update, opts);
    return () => {
      window.removeEventListener("pointerdown", update, opts);
      window.removeEventListener("pointermove", update, opts);
    };
  },
);
