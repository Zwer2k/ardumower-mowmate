<script lang="ts">
    import { Dropdown, Toggle, TextInput } from "carbon-components-svelte";
    import IconChevronUp from "carbon-icons-svelte/lib/ChevronUp.svelte";
    import IconChevronDown from "carbon-icons-svelte/lib/ChevronDown.svelte";
    import VirtualList from "../../../widget/VirtualList.svelte";
    import type { LogLevelDescT, LogLine } from "../../../model";
    import type { DropdownItem } from "carbon-components-svelte/src/Dropdown/Dropdown.svelte";

    export let logLevels: LogLevelDescT;
    export let dbgLevels: DropdownItem[];
    export let logData: LogLine[];
    export let dbgLevel: number;

    // The modem numbers every log line consecutively (LogLine.nr). A gap in
    // that sequence therefore proves lines were dropped: either overwritten in
    // the modem's ring buffer before they could be sent, or lost on the way.
    // gapBefore holds how many lines are missing directly above this line.
    type LogRow = LogLine & { gapBefore: number };

    let items: LogRow[] = [];
    let seenLogNumbers = new Set<number>();
    // Highest line number appended so far, for gap detection.
    let lastNr: number | null = null;
    // Lines this view dropped itself once the logLines cap was reached.
    let trimmedCount = 0;

    let autoscroll = true;

	let scrollToIndex: ((index: any, opts: any) => Promise<void>) | undefined = undefined;
    let logLevelIndex: number | undefined;
    let logLines = 10000;

    // ─── Search ──────────────────────────────────────────────────────────────
    let searchQuery = "";
    let searchMatches: number[] = [];
    let searchCurrent = -1;
    let highlightIndex = -1;

    $: {
        if (searchQuery.trim() === "") {
            searchMatches = [];
            searchCurrent = -1;
            highlightIndex = -1;
        } else {
            const q = searchQuery.toLowerCase();
            searchMatches = items
                .map((item, idx) => ({ idx, text: (item.text || "").toLowerCase() }))
                .filter(({ text }) => text.includes(q))
                .map(({ idx }) => idx);
            if (searchMatches.length > 0 && searchCurrent === -1) {
                searchCurrent = 0;
                highlightIndex = searchMatches[0];
                if (scrollToIndex) scrollToIndex(highlightIndex, { behavior: 'smooth' });
            }
        }
    }

    function nextSearchMatch() {
        if (searchMatches.length === 0) return;
        searchCurrent = (searchCurrent + 1) % searchMatches.length;
        highlightIndex = searchMatches[searchCurrent];
        if (scrollToIndex) scrollToIndex(highlightIndex, { behavior: 'smooth' });
    }

    function prevSearchMatch() {
        if (searchMatches.length === 0) return;
        searchCurrent = (searchCurrent - 1 + searchMatches.length) % searchMatches.length;
        highlightIndex = searchMatches[searchCurrent];
        if (scrollToIndex) scrollToIndex(highlightIndex, { behavior: 'smooth' });
    }

    $: {
        if (logData && logData.length > 0) {
            const newLines = logData.filter(line => !seenLogNumbers.has(line.nr));
            if (newLines.length > 0) {
                // Determine the gap once while appending, not while rendering.
                // The virtual list only renders visible rows and rebuilds them
                // on every scroll, so a counter evaluated in the template
                // depends on the scroll history instead of on the data.
                const newRows: LogRow[] = newLines.map(line => {
                    const gapBefore =
                        lastNr != null && line.nr > lastNr + 1 ? line.nr - lastNr - 1 : 0;
                    if (lastNr == null || line.nr > lastNr) lastNr = line.nr;
                    seenLogNumbers.add(line.nr);
                    return { ...line, gapBefore };
                });
                items = [...items, ...newRows];
                let remove = items.length - logLines;
                if (remove > 0) {
                    // Forget the numbers of the removed lines as well, else the
                    // set grows without bound over a long session.
                    for (const dropped of items.slice(0, remove)) {
                        seenLogNumbers.delete(dropped.nr);
                    }
                    items.splice(0, remove);
                    trimmedCount += remove;
                }
                if (autoscroll && scrollToIndex != undefined) {
                    scrollToIndex(items.length - 1, { behavior: 'smooth' });
                }
            }
        }
    }

    // function onScroll() {
    //     autoscroll = list.getOffset() > list.getScrollSize() - list.getClientSize() - 10;
    // }

    logLevelIndex = dbgLevels.findIndex(item => item.id == dbgLevel);
    if (logLevelIndex == -1)  {
        logLevelIndex = 3;
    }

    function onBottom() {
        console.log("bottom");
    }

    $: { dbgLevel = dbgLevels[logLevelIndex].id; }

    function downloadLog() {
        const header = "nr,level,freeHeap,text\r\n";
        let csv = header;
        for (const line of items) {
            let text = line.text.replace(/"/g, '""').replace(/\r\n/g, " ").replace(/\r/g, " ").replace(/\n/g, " ");
            csv += `${line.nr},${line.level},${line.freeHeap},"${text}"\r\n`;
        }
        const blob = new Blob([csv], { type: "text/csv;charset=utf-8" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        const now = new Date().toISOString().replace(/[:.]/g, "-");
        a.href = url;
        a.download = `ardumower-log-${now}.csv`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

</script>

<div class="modem-log">
    <div class="settings">
        <Dropdown
            bind:selectedId={dbgLevel}
            type="inline"
            titleText="Log level"
            items={dbgLevels}/>
        <Toggle
            class="autoscroll-toggle"
            labelText="Autoscroll"
            labelA={""}
            labelB={""}
            bind:toggled={autoscroll}/>
        <div class="search-box">
            <TextInput
                size="sm"
                placeholder="Search log..."
                bind:value={searchQuery}
            />
            {#if searchMatches.length > 0}
                <span class="search-count">{searchCurrent + 1} / {searchMatches.length}</span>
                <button class="search-btn" on:click={prevSearchMatch} title="Previous">
                    <IconChevronUp />
                </button>
                <button class="search-btn" on:click={nextSearchMatch} title="Next">
                    <IconChevronDown />
                </button>
            {/if}
        </div>
        <button class="export-link" on:click={downloadLog}>
            Download log
        </button>
    </div>
    {#if trimmedCount > 0}
        <div class="log-trimmed">{trimmedCount} ältere Zeilen ausgeblendet (Anzeigegrenze {logLines})</div>
    {/if}
    <div class="log-list">
        <VirtualList {items}
            height="100%"
            bind:scrollToIndex
            let:item
            let:index>
            {#if (item as LogRow).gapBefore > 0}
                <div class="log-gap" title="Lücke in den Zeilennummern: diese Zeilen sind im Modem verworfen worden, bevor sie den Browser erreicht haben">
                    {(item as LogRow).gapBefore}
                    {(item as LogRow).gapBefore === 1 ? 'Zeile verworfen' : 'Zeilen verworfen'}
                </div>
            {/if}
            <div class="log-line {index === highlightIndex ? 'highlight' : ''}">
                <div class="nr">{item.nr}:</div>
                <div class="level level-{logLevels[(item as LogLine).level]}">{logLevels[(item as LogLine).level]}:</div>
                <div class="free-heap">{(item as LogLine).freeHeap}:</div>
                <div class="text">{item.text}</div>
            </div>
        </VirtualList>
    </div>
</div>

<style lang="scss">
    .modem-log {
        :global(.bx--toggle-input__label .bx--toggle__switch) {
            margin-top: 0;
        }

        :global(.autoscroll-toggle) {
            display: inline-grid;
        }

        :global(.autoscroll-toggle label) {
            display: flex;
            flex-direction: row;
            align-items: center;
            font-size: .875rem;
            margin-left: 10px;
        }

        :global(.autoscroll-toggle label :first-child) {
            margin: 8px;
        }
    }

    .modem-log {
        display: flex;
        flex-direction: column;
        height: 100%;
        width: 100%;
    }

    .settings {
        border-bottom: 1px solid lightgray;
        padding: 0 10px;
        flex-shrink: 0;
    }

    .log-list {
        flex: 1;
        width: 100%;
        border: 1px solid lightgray;
        overflow: hidden;
    }

    .log-line {
        display: flex;
        padding: 3px;
    }

    .log-gap {
        display: flex;
        align-items: center;
        gap: 8px;
        margin: 4px 0;
        padding: 1px 6px;
        font-size: 0.75rem;
        font-weight: 600;
        color: #a2191f;
        background-color: #fff1f1;
        border-top: 1px dashed #da1e28;
        border-bottom: 1px dashed #da1e28;
    }

    .log-trimmed {
        flex-shrink: 0;
        padding: 2px 10px;
        font-size: 0.75rem;
        color: #555;
        background-color: #f4f4f4;
        border-bottom: 1px solid lightgray;
    }

    .log-line .nr, .log-line .level, .log-line .free-heap {
        margin-right: 3px;
    }

    .log-line .nr {
        min-width: 30px;
    }

    .log-line .level {
        min-width: 39px;
    }

    .log-line .free-heap {
        min-width: 50px;
    }

    .level-COMM {
        color: blue;
    }

    .level-INFO {
        color: green;
    }

    .level-WARN {
        color: orange;
    }

    .level-ERR {
        color: red;
    }

    .level-CRIT {
        color: red;
        border: 1px solid red;
    }

    .log-line .text {
        line-break: anywhere;
    }

    .export-link {
        display: inline-flex;
        align-items: center;
        height: 32px;
        padding: 0 12px;
        margin-left: 10px;
        font-family: inherit;
        font-size: .875rem;
        font-weight: 400;
        line-height: 1.125rem;
        letter-spacing: .16px;
        color: #161616;
        background-color: #e0e0e0;
        border: none;
        border-radius: 0;
        text-decoration: none;
        cursor: pointer;
        transition: background-color 70ms cubic-bezier(0,0,.38,.9);
    }

    .search-box {
        display: inline-flex;
        align-items: center;
        gap: 4px;
        margin-left: 10px;
    }

    .search-box :global(.bx--text-input) {
        height: 32px;
        min-width: 160px;
    }

    .search-count {
        font-size: 0.75rem;
        color: #555;
        min-width: 40px;
        text-align: center;
        font-family: monospace;
    }

    .search-btn {
        display: inline-flex;
        align-items: center;
        justify-content: center;
        width: 28px;
        height: 28px;
        padding: 0;
        border: 1px solid #ccc;
        border-radius: 3px;
        background: #f4f4f4;
        cursor: pointer;
    }

    .search-btn:hover {
        background: #e0e0e0;
    }

    .highlight {
        background-color: #fff3cd !important;
        border-left: 3px solid #ffab00;
        padding-left: 0;
    }

    .export-link:hover {
        background-color: #cacaca;
    }
</style>
