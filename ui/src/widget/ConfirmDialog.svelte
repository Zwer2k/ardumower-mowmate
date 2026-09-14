<script lang="ts">
  import {
    ComposedModal,
    ModalHeader,
    ModalBody,
    ModalFooter,
    Button,
  } from "carbon-components-svelte";
  import { confirmDialogStore, closeConfirm } from "../stores/confirm-dialog";

  let state = $state($confirmDialogStore);
  confirmDialogStore.subscribe((s) => (state = s));

  const open = $derived(!!state);
  const title = $derived(state?.title ?? "");
  const message = $derived(state?.message ?? "");
  const confirmText = $derived(state?.confirmText ?? "OK");
  const cancelText = $derived(state?.cancelText ?? "Abbrechen");
  const dismissText = $derived(state?.dismissText ?? "");
  const confirmKind = $derived(state?.kind === "danger" ? "danger" : "primary");
</script>

<ComposedModal
  {open}
  on:close={() => closeConfirm("dismiss")}
  preventCloseOnClickOutside
>
  <ModalHeader {title} />
  <ModalBody>
    <p>{message}</p>
  </ModalBody>
  <ModalFooter>
    {#if dismissText}
      <Button kind="ghost" on:click={() => closeConfirm("dismiss")}>
        {dismissText}
      </Button>
    {/if}
    <Button kind="secondary" on:click={() => closeConfirm("cancel")}>
      {cancelText}
    </Button>
    <Button kind={confirmKind} on:click={() => closeConfirm("confirm")}>
      {confirmText}
    </Button>
  </ModalFooter>
</ComposedModal>
