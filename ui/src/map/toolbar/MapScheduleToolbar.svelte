<script lang="ts">
    import {
        Button,
        Row,
        Column,
        StructuredList,
        StructuredListBody,
        StructuredListCell,
        StructuredListHead,
        StructuredListRow,
        TextInput,
        TimePicker,
        Dropdown,
        Checkbox,
        Toggle,
        FormGroup,
        ButtonSet,
    } from "carbon-components-svelte";
    import IconAdd from "carbon-icons-svelte/lib/Add.svelte";
    import IconTrashCan from "carbon-icons-svelte/lib/TrashCan.svelte";
    import IconSave from "carbon-icons-svelte/lib/Save.svelte";
    import IconCalendar from "carbon-icons-svelte/lib/Calendar.svelte";
    import {
        scheduleStore,
        nextEntryId,
        newScheduleEntry,
        hasWeekDay,
        toggleWeekDay,
        hasMonthDay,
        toggleMonthDay,
        useCurrentMap,
    } from "../../stores/schedule";
    import { socketStore } from "../../stores/socket";

    const weekDays = [
        { day: 0, label: "So" },
        { day: 1, label: "Mo" },
        { day: 2, label: "Di" },
        { day: 3, label: "Mi" },
        { day: 4, label: "Do" },
        { day: 5, label: "Fr" },
        { day: 6, label: "Sa" },
    ];

    const monthDays = Array.from({ length: 31 }, (_, i) => i + 1);

    const modeItems = [
        { id: "0", text: "Täglich" },
        { id: "1", text: "Wöchentlich" },
        { id: "2", text: "Monatlich" },
    ];

    let selectedEntryId: number | null = null;
    $: entries = $scheduleStore.entries;
    $: selectedEntry = entries.find((e) => e.id === selectedEntryId) ?? null;
    $: currentMap = useCurrentMap($socketStore.maps, $socketStore.currentMapId);
    $: canAdd = currentMap != null;
    $: hasScheduleDirty = $scheduleStore.dirty;

    let currentMapId: string | null = null;
    $: if ($socketStore.currentMapId !== currentMapId) {
        currentMapId = $socketStore.currentMapId;
        selectedEntryId = null;
    }

    function addEntry() {
        if (!currentMap) return;
        const entry = newScheduleEntry(currentMap.id, currentMap.name);
        entry.id = nextEntryId(entries);
        scheduleStore.update((s) => {
            const next = { ...s, entries: [...s.entries, entry] };
            return next;
        });
        selectedEntryId = entry.id;
        scheduleStore.commitDraft();
    }

    function removeEntry(id: number) {
        scheduleStore.update((s) => ({
            ...s,
            entries: s.entries.filter((e) => e.id !== id),
        }));
        if (selectedEntryId === id) selectedEntryId = null;
        scheduleStore.commitDraft();
    }

    function updateEntry(id: number, patch: Partial<typeof selectedEntry>) {
        scheduleStore.update((s) => ({
            ...s,
            entries: s.entries.map((e) =>
                e.id === id ? { ...e, ...patch } : e,
            ),
        }));
        scheduleStore.commitDraft();
    }

    function onSelectMode(
        entry: typeof selectedEntry,
        event: CustomEvent<{ selectedId: string }>,
    ) {
        if (!entry) return;
        const mode = parseInt(event.detail.selectedId, 10) as 0 | 1 | 2;
        updateEntry(entry.id, { mode });
    }

    function onTimeChange(entry: typeof selectedEntry, event: Event) {
        if (!entry || !event.currentTarget) return;
        const value = (event.currentTarget as HTMLInputElement).value;
        if (!value) return;
        const [hh, mm] = value.split(":").map((x) => parseInt(x, 10));
        if (isNaN(hh) || isNaN(mm)) return;
        updateEntry(entry.id, {
            hour: Math.max(0, Math.min(23, hh)),
            minute: Math.max(0, Math.min(59, mm)),
        });
    }

    function formatTime(h: number, m: number): string {
        return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}`;
    }

    function formatMode(entry: (typeof entries)[0]): string {
        if (entry.mode === 0) return "Täglich";
        if (entry.mode === 1) return "Wöchentlich";
        if (entry.mode === 2) return "Monatlich";
        return "?";
    }
</script>

<Row class="map-schedule-row">
    <Column>
        <div class="toolbar-btn-row">
            <Button
                kind={hasScheduleDirty ? "danger" : "primary"}
                size="small"
                disabled={!hasScheduleDirty}
                icon={IconSave}
                iconDescription="Schedule speichern"
                on:click={() => scheduleStore.save()}
            >
                <span class="btn-label">Speichern</span>
            </Button>
            <Button
                kind="secondary"
                size="small"
                disabled={!canAdd}
                icon={IconAdd}
                iconDescription="Termin hinzufügen"
                on:click={addEntry}
            >
                <span class="btn-label">Hinzufügen</span>
            </Button>
            {#if currentMap}
                <span class="map-hint"
                    >Karte: <strong>{currentMap.name}</strong></span
                >
            {:else}
                <span class="map-hint warn">Keine Karte geladen</span>
            {/if}
            <div
                class="schedule-toggle"
                title="Scheduler aktiviert/deaktiviert"
            >
                <Toggle
                    hideLabel
                    labelText=""
                    toggled={$scheduleStore.enabled}
                    on:toggle={(e) => {
                        if (e.detail.toggled === $scheduleStore.enabled) return;
                        scheduleStore.update((s) => ({
                            ...s,
                            enabled: e.detail.toggled,
                        }));
                        scheduleStore.commitDraft();
                    }}
                />
            </div>
        </div>

        <div class="schedule-form">
            <div class="schedule-list-wrapper">
                <StructuredList>
                    <StructuredListHead>
                        <StructuredListRow head>
                            <StructuredListCell head>Aktiv</StructuredListCell>
                            <StructuredListCell head>Name</StructuredListCell>
                            <StructuredListCell head>Uhrzeit</StructuredListCell
                            >
                            <StructuredListCell head
                                >Wiederholung</StructuredListCell
                            >
                            <StructuredListCell head>Karte</StructuredListCell>
                            <StructuredListCell head></StructuredListCell>
                        </StructuredListRow>
                    </StructuredListHead>
                    <StructuredListBody>
                        {#each entries as entry (entry.id)}
                            <StructuredListRow
                                on:click={() => (selectedEntryId = entry.id)}
                                class={selectedEntryId === entry.id
                                    ? "selected"
                                    : ""}
                            >
                                <StructuredListCell class="entry-cell">
                                    <span class="entry-row-inline">
                                        <!-- Carbon Checkbox dispatches "check"
                                             on mount and on every external
                                             checked-change, not only on user
                                             clicks. Only treat it as a value
                                             change if it differs from the
                                             current entry state, otherwise the
                                             schedule would be marked dirty on
                                             every render. -->
                                        <Checkbox
                                            hideLabel
                                            labelText="Aktiv"
                                            checked={entry.enabled}
                                            on:check={(e) => {
                                                if (
                                                    e.detail === entry.enabled
                                                )
                                                    return;
                                                updateEntry(entry.id, {
                                                    enabled: e.detail,
                                                });
                                            }}
                                        />
                                    </span>
                                </StructuredListCell>
                                <StructuredListCell
                                    >{entry.name}</StructuredListCell
                                >
                                <StructuredListCell
                                    >{formatTime(
                                        entry.hour,
                                        entry.minute,
                                    )}</StructuredListCell
                                >
                                <StructuredListCell
                                    >{formatMode(entry)}</StructuredListCell
                                >
                                <StructuredListCell
                                    >{entry.mapName}</StructuredListCell
                                >
                                <StructuredListCell>
                                    <Button
                                        kind="danger"
                                        size="small"
                                        icon={IconTrashCan}
                                        iconDescription="Löschen"
                                        on:click={() => removeEntry(entry.id)}
                                    />
                                </StructuredListCell>
                            </StructuredListRow>
                        {:else}
                            <StructuredListRow>
                                <StructuredListCell style="grid-column: span 5"
                                    >Keine Termine. Füge einen mit der aktuellen
                                    Karte hinzu.</StructuredListCell
                                >
                            </StructuredListRow>
                        {/each}
                    </StructuredListBody>
                </StructuredList>
            </div>

            {#if selectedEntry}
                {@const entry = selectedEntry}
                <div class="entry-edit entry-edit-scroll">
                    <TextInput
                        labelText="Name"
                        bind:value={entry.name}
                        on:change={() =>
                            updateEntry(entry.id, { name: entry.name })}
                    />
                    <TimePicker
                        labelText="Uhrzeit"
                        value={formatTime(entry.hour, entry.minute)}
                        on:change={(e) => onTimeChange(entry, e)}
                    />
                    <Dropdown
                        titleText="Wiederholung"
                        items={modeItems}
                        selectedId={String(entry.mode)}
                        on:select={(e) => onSelectMode(entry, e)}
                    />
                    {#if entry.mode === 1}
                        <FormGroup legendText="Wochentage">
                            <div class="weekday-grid">
                                {#each weekDays as wd}
                                    <Button
                                        kind={hasWeekDay(
                                            entry.daysOfWeek,
                                            wd.day,
                                        )
                                            ? "primary"
                                            : "tertiary"}
                                        size="small"
                                        class="weekday-btn"
                                        on:click={() =>
                                            updateEntry(entry.id, {
                                                daysOfWeek: toggleWeekDay(
                                                    entry.daysOfWeek,
                                                    wd.day,
                                                ),
                                            })}
                                    >
                                        {wd.label}
                                    </Button>
                                {/each}
                            </div>
                        </FormGroup>
                    {/if}
                    {#if entry.mode === 2}
                        <FormGroup legendText="Monatstage">
                            <div class="month-grid">
                                {#each monthDays as day}
                                    <Button
                                        kind={hasMonthDay(
                                            entry.daysOfMonth,
                                            day,
                                        )
                                            ? "primary"
                                            : "tertiary"}
                                        size="small"
                                        class="monthday-btn"
                                        on:click={() =>
                                            updateEntry(entry.id, {
                                                daysOfMonth: toggleMonthDay(
                                                    entry.daysOfMonth,
                                                    day,
                                                ),
                                            })}
                                    >
                                        {day}
                                    </Button>
                                {/each}
                            </div>
                        </FormGroup>
                    {/if}
                    <div class="map-readonly">
                        <IconCalendar size={16} />
                        Karte für diesen Termin:
                        <strong>{entry.mapName}</strong>
                    </div>
                </div>
            {/if}
        </div>
    </Column>
</Row>

<style>
    .toolbar-btn-row {
        display: flex;
        flex: 0 0 auto;
        flex-wrap: wrap;
        align-items: center;
        gap: 0.5rem;
        margin-bottom: 0.5rem;
    }
    .btn-label {
        margin-left: 0.25rem;
    }
    .toolbar-btn-row :global(.bx--btn) {
        padding-left: 0.3125rem !important;
        padding-right: 0.3125rem !important;
    }
    .toolbar-btn-row :global(.bx--btn .bx--btn__icon) {
        position: static;
        margin-left: 0.5rem;
        margin-right: 0.25rem;
    }
    .map-hint {
        font-size: 0.875rem;
        color: #525252;
    }
    .map-hint.warn {
        color: #da1e28;
    }
    .schedule-toggle {
        display: inline-flex;
        align-items: center;
        margin-left: auto;
    }
    .schedule-toggle :global(.bx--toggle) {
        min-width: 0;
    }
    .schedule-toggle :global(.bx--toggle__label) {
        margin-bottom: 0;
    }
    .schedule-toggle :global(.bx--toggle__label-text) {
        display: none;
    }
    .schedule-toggle :global(.bx--toggle__text) {
        display: none;
    }
    .schedule-form {
        display: flex;
        flex-direction: column;
        min-height: 0;
        overflow: visible;
    }
    .schedule-list-wrapper {
        flex: 1 1 auto;
        min-height: 8rem;
        max-height: 35vh;
        overflow-y: auto;
        margin-bottom: 0.25rem;
    }
    .schedule-form :global(.bx--form-item) {
        margin-bottom: 0.25rem;
    }
    .schedule-form :global(.bx--structured-list) {
        margin-bottom: 0.25rem;
    }
    .schedule-form :global(.bx--structured-list-row) {
        min-height: 1.75rem;
    }
    .schedule-form :global(.bx--structured-list-th) {
        padding-top: 0.25rem;
        padding-bottom: 0.25rem;
    }
    .schedule-form :global(.bx--structured-list-td) {
        padding-top: 0.25rem;
        padding-bottom: 0.25rem;
    }
    .entry-cell {
        overflow: visible;
        white-space: nowrap;
    }
    .entry-row-inline {
        display: inline-flex;
        align-items: center;
        gap: 0.5rem;
    }
    .entry-row-inline :global(.bx--checkbox-wrapper) {
        display: inline-flex;
        align-items: center;
        margin-top: 0;
    }
    .entry-row-inline :global(.bx--checkbox-label) {
        padding-left: 0;
        min-height: unset;
        display: inline-flex;
        align-items: center;
    }
    .entry-row-inline :global(.bx--checkbox-label::before) {
        position: static;
        margin: 0;
    }
    .entry-name {
        display: inline;
    }
    .entry-edit {
        flex: 0 0 auto;
        margin-top: 0.5rem;
        padding: 0.5rem;
        background: #f4f4f4;
        border-radius: 4px;
        max-height: 45vh;
        overflow: visible;
        display: flex;
        flex-direction: column;
        min-height: 0;
    }
    .entry-edit-scroll {
        flex: 0 1 auto;
        min-height: 0;
        overflow-y: auto;
        margin-bottom: 0.5rem;
    }
    .entry-edit-scroll :global(.bx--form-item) {
        margin-bottom: 0.5rem;
    }
    .weekday-grid {
        display: grid;
        grid-template-columns: repeat(7, 1fr);
        gap: 0.25rem;
    }
    .weekday-grid :global(.bx--btn) {
        width: 100%;
        padding-left: 0;
        padding-right: 0;
        min-width: 0;
        justify-content: center;
        text-align: center;
    }

    /* Selected weekday buttons: white text on strong blue.
       Unselected: dark text on light grey background for high contrast. */
    .weekday-grid :global(.bx--btn--primary) {
        color: #ffffff !important;
        background-color: #0f62fe;
        border-color: #0f62fe;
        box-shadow: inset 0 0 0 2px #0f62fe;
    }
    .weekday-grid :global(.bx--btn--tertiary) {
        color: #161616;
        background-color: #e0e0e0;
        border-color: #e0e0e0;
    }

    .month-grid {
        display: grid;
        grid-template-columns: repeat(7, 1fr);
        gap: 0.25rem;
    }

    .month-grid :global(.bx--btn) {
        width: 100%;
        padding-left: 0;
        padding-right: 0;
        min-width: 0;
        justify-content: center;
        text-align: center;
    }

    .month-grid :global(.bx--btn--primary) {
        color: #ffffff !important;
        background-color: #0f62fe;
        border-color: #0f62fe;
        box-shadow: inset 0 0 0 2px #0f62fe;
    }
    .month-grid :global(.bx--btn--tertiary) {
        color: #161616;
        background-color: #e0e0e0;
        border-color: #e0e0e0;
    }
    .map-readonly {
        display: flex;
        align-items: center;
        gap: 0.5rem;
        margin-top: 0.5rem;
        font-size: 0.875rem;
    }
    @media (max-width: 800px) {
        .btn-label {
            display: none;
        }
        .toolbar-btn-row :global(.bx--btn) {
            width: 2rem;
            padding: 0 !important;
            justify-content: center;
        }
        .toolbar-btn-row :global(.bx--btn .bx--btn__icon) {
            position: static;
            margin: 0;
        }
        .month-grid {
            grid-template-columns: repeat(7, 1fr);
        }
    }
    :global(.bx--structured-list-row.selected) {
        background: #e0e0e0;
    }
</style>
