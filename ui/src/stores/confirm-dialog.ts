import { writable } from "svelte/store";

export type ConfirmResult = "confirm" | "cancel" | "dismiss";

export interface ConfirmOptions {
  title: string;
  message: string;
  confirmText?: string;
  cancelText?: string;
  /** Optional third button that neither confirms nor cancels (e.g. "Abbrechen"
   *  next to "Speichern"/"Verwerfen"). Closing via Escape/X resolves the same
   *  way, so a dialog with two destructive outcomes can always be escaped. */
  dismissText?: string;
  kind?: "primary" | "danger";
}

export interface ConfirmState extends ConfirmOptions {
  resolve: (value: ConfirmResult) => void;
}

export const confirmDialogStore = writable<ConfirmState | null>(null);

export function openConfirmChoice(options: ConfirmOptions): Promise<ConfirmResult> {
  return new Promise((resolve) => {
    confirmDialogStore.set({ ...options, resolve });
  });
}

/** Two-way confirm: true only for the confirm button; cancel and dismiss both
 *  resolve to false. Use openConfirmChoice() when they must be distinguished. */
export function openConfirm(options: ConfirmOptions): Promise<boolean> {
  return openConfirmChoice(options).then((result) => result === "confirm");
}

export function closeConfirm(value: boolean | ConfirmResult) {
  const result: ConfirmResult = typeof value === "boolean" ? (value ? "confirm" : "cancel") : value;
  confirmDialogStore.update((state) => {
    if (state) state.resolve(result);
    return null;
  });
}
