<script lang="ts">
  // Auswahlliste im Aussehen von Carbons Dropdown, deren aufgeklappte Liste am
  // <body> hängt (fest am Knopf ausgerichtet). Carbon 0.89 kann das nicht; in
  // Scrollbereichen (Zeitplaner, Dialoge) wurde die Liste dort abgeschnitten.
  import { createEventDispatcher, onDestroy, tick } from "svelte";
  import ChevronDown from "carbon-icons-svelte/lib/ChevronDown.svelte";
  import { portal } from "./portal";

  export let items: { id: string; text: string }[] = [];
  export let selectedId: string | undefined = undefined;
  export let titleText = "";
  /** Text, solange nichts gewählt ist */
  export let label = "";
  export let size: "sm" | undefined = undefined;
  export let disabled = false;

  const dispatch = createEventDispatcher<{ select: { selectedId: string } }>();

  let open = false;
  let field: HTMLButtonElement;
  let menuStyle = "";
  $: selected = items.find((i) => i.id === selectedId);

  const MENU_MAX = 240;

  function place() {
    if (!field) return;
    const r = field.getBoundingClientRect();
    const below = window.innerHeight - r.bottom;
    const above = r.top;
    // unten, außer dort ist zu wenig Platz und oben mehr
    const up = below < Math.min(MENU_MAX, 160) && above > below;
    const max = Math.max(80, Math.min(MENU_MAX, (up ? above : below) - 8));
    menuStyle = `left:${r.left}px;width:${r.width}px;max-height:${max}px;` +
      (up ? `bottom:${window.innerHeight - r.top}px;` : `top:${r.bottom}px;`);
  }

  async function toggle() {
    if (disabled) return;
    open = !open;
    if (open) {
      place();
      await tick();
      document.querySelector<HTMLElement>(".portal-dropdown-menu [aria-selected='true']")?.scrollIntoView?.({ block: "nearest" });
    }
  }

  function choose(id: string) {
    open = false;
    field?.focus();
    if (id !== selectedId) {
      selectedId = id;
      dispatch("select", { selectedId: id });
    }
  }

  // Außerhalb klicken, Escape, Größe ändern: zu; Scrollen: mitwandern, bis die
  // Auswahl den Scrollbereich verlässt
  function onWindowPointer(e: PointerEvent) {
    if (!open) return;
    const t = e.target as Node;
    if (field?.contains(t) || (t as HTMLElement).closest?.(".portal-dropdown-menu")) return;
    open = false;
  }
  function onKey(e: KeyboardEvent) {
    if (open && e.key === "Escape") { open = false; field?.focus(); }
  }
  // Nächster Scrollbereich um den Knopf (Editor des Zeitplaners, Dialog)
  function scrollParent(el: HTMLElement | null): HTMLElement | null {
    for (let p = el?.parentElement ?? null; p; p = p.parentElement) {
      const o = getComputedStyle(p).overflowY;
      if (o === "auto" || o === "scroll") return p;
    }
    return null;
  }
  function onScroll(e: Event) {
    if (!open) return;
    if ((e.target as HTMLElement)?.closest?.(".portal-dropdown-menu")) return;
    // Verlässt die Auswahl den sichtbaren Teil ihres Scrollbereichs, klappt
    // die Liste zu, statt frei über anderen Inhalten zu schweben
    const area = scrollParent(field);
    if (area && field) {
      const a = area.getBoundingClientRect(), r = field.getBoundingClientRect();
      if (r.top < a.top || r.bottom > a.bottom) {
        open = false;
        return;
      }
    }
    place();
  }
  function onResize() { open = false; }
  if (typeof window !== "undefined") {
    window.addEventListener("pointerdown", onWindowPointer, true);
    window.addEventListener("keydown", onKey);
    window.addEventListener("scroll", onScroll, true);
    window.addEventListener("resize", onResize);
  }
  onDestroy(() => {
    if (typeof window === "undefined") return;
    window.removeEventListener("pointerdown", onWindowPointer, true);
    window.removeEventListener("keydown", onKey);
    window.removeEventListener("scroll", onScroll, true);
    window.removeEventListener("resize", onResize);
  });
</script>

<div class="bx--form-item portal-dropdown">
  {#if titleText}<span class="bx--label">{titleText}</span>{/if}
  <div class="bx--dropdown bx--list-box" class:bx--list-box--sm={size === "sm"} class:bx--list-box--expanded={open}
    class:bx--list-box--disabled={disabled}>
    <button bind:this={field} type="button" class="bx--list-box__field" aria-haspopup="listbox" aria-expanded={open}
      {disabled} on:click={toggle}>
      <span class="bx--list-box__label">{selected ? selected.text : label}</span>
      <span class="bx--list-box__menu-icon" class:bx--list-box__menu-icon--open={open}><ChevronDown /></span>
    </button>
  </div>
</div>

{#if open}
  <div use:portal class="portal-dropdown-menu" role="listbox" style={menuStyle}>
    {#each items as item (item.id)}
      <div class="bx--list-box__menu-item" class:bx--list-box__menu-item--active={item.id === selectedId}>
        <button type="button" role="option" aria-selected={item.id === selectedId}
          class="bx--list-box__menu-item__option" on:click={() => choose(item.id)}>{item.text}</button>
      </div>
    {/each}
  </div>
{/if}

<style>
  .portal-dropdown .bx--list-box__field {
    width: 100%;
    background: none;
    border: none;
    text-align: left;
    color: inherit;
    font: inherit;
    cursor: pointer;
  }
  :global(.portal-dropdown-menu) {
    position: fixed;
    z-index: 10000;
    overflow-y: auto;
    background: var(--cds-field-01, #f4f4f4);
    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.3);
  }
  :global(.portal-dropdown-menu .bx--list-box__menu-item) {
    height: auto;
  }
  :global(.portal-dropdown-menu .bx--list-box__menu-item__option) {
    display: block;
    width: 100%;
    margin: 0;
    padding: 0.6875rem 1rem;
    border: none;
    background: none;
    text-align: left;
    font: inherit;
    color: var(--cds-text-01, #161616);
    cursor: pointer;
  }
  :global(.portal-dropdown-menu .bx--list-box__menu-item:hover),
  :global(.portal-dropdown-menu .bx--list-box__menu-item--active) {
    background: var(--cds-hover-ui, #e5e5e5);
  }
</style>
