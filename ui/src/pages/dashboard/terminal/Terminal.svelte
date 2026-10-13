


<script lang="ts">
    import type { ConsoleLine } from '../../../model';
    import VirtualList from '../../../widget/VirtualList.svelte';
      import { Toggle } from 'carbon-components-svelte';
    import { ubxCommands } from '../gps/ubxCommands';
    
    let  { 
      consoleLines,
      sendCmd = $bindable(),
      onOutputDone 
    }: { 
      consoleLines: ConsoleLine[],
      sendCmd: string,
      onOutputDone?: () => void
    } = $props();  
  
    const HISTORY_KEY = 'ardumower_terminal_history';
    const MAX_HISTORY = 50;
  
    let autoscroll = $state(true);
    let bufferLines = 10000;
    
    let command = $state<string>("");
    let output = $state<ConsoleLine[]>([]);
    let history = $state<string[]>(loadHistory());
    let historyIndex = $state<number>(-1);
    let commandIndex = 0;
    let scrollToIndex: ((index: any, opts: any) => Promise<void>) | undefined = $state(undefined);

    // Known commands context menu
    let showCmdMenu = $state(false);
    let menuPos = $state({ x: 0, y: 0 });
    let menuAbove = $state(false);
    let inputEl: HTMLInputElement | undefined = $state(undefined);

    interface KnownCommand { cmd: string; desc: string; group: string }

    // Nach doc/AT_Protocol.md (Sunray). "MowMate-Sunray": nur im Sunray-Fork,
    // upstream Sunray antwortet darauf nicht. Kartenupload (AT+W/N/X) fehlt
    // bewusst, er überschreibt die Karte im Mäher.
    const baseCommands: KnownCommand[] = [
      { group: 'Abfragen', cmd: 'AT+V', desc: 'Version und Board' },
      { group: 'Abfragen', cmd: 'AT+S', desc: 'Status (Akku, Position, Betrieb, GPS, Karten-CRC)' },
      { group: 'Abfragen', cmd: 'AT+S2', desc: 'Hindernisse' },
      { group: 'Abfragen', cmd: 'AT+S3', desc: 'Sensoren: Sonar, Bumper, Lift, Regen (MowMate-Sunray)' },
      { group: 'Abfragen', cmd: 'AT+S4', desc: 'GPS-Details und Satelliten (MowMate-Sunray)' },
      { group: 'Abfragen', cmd: 'AT+S5', desc: 'Motordrehzahlen und -ströme (MowMate-Sunray)' },
      { group: 'Abfragen', cmd: 'AT+T', desc: 'Statistiken' },
      { group: 'Abfragen', cmd: 'AT+L', desc: 'Statistiken löschen' },
      { group: 'Steuern', cmd: 'AT+C,-1,1,-1,-1,-1,-1,-1,-1', desc: 'Mähen starten' },
      { group: 'Steuern', cmd: 'AT+C,-1,0,-1,-1,-1,-1,-1,-1', desc: 'Stopp (Idle, alle Motoren aus)' },
      { group: 'Steuern', cmd: 'AT+C,-1,4,-1,-1,-1,-1,-1,-1', desc: 'Docken' },
      { group: 'Steuern', cmd: 'AT+C,1,-1,-1,-1,-1,-1,-1,-1', desc: 'Mähmotor ein (0 = aus)' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,0.30,-1,-1,-1,-1,-1', desc: 'Mähgeschwindigkeit (m/s)' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,-1,60,-1,-1,-1,-1', desc: 'GPS-Fix-Timeout (s)' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,-1,-1,-1,50,-1,-1', desc: 'Mähfortschritt setzen (%)' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,-1,-1,-1,-1,1,-1', desc: 'Nächsten Mähpunkt überspringen' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,-1,-1,-1,-1,-1,1', desc: 'Sonar ein (0 = aus)' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,-1,-1,-1,-1,-1,-1,179,-1,-1', desc: 'Max. Mähmotor-PWM (0..255)' },
      { group: 'Steuern', cmd: 'AT+C,-1,-1,-1,-1,-1,-1,-1,-1,-1,45,-1', desc: 'Schnitthöhe (mm, falls unterstützt)' },
      { group: 'Steuern', cmd: 'AT+M,0,0', desc: 'Manuell fahren (linear m/s, angular rad/s)' },
      { group: 'Steuern', cmd: 'AT+R,0,0', desc: 'Zu Kartenpunkt fahren (x, y in m; MowMate-Sunray)' },
      { group: 'Steuern', cmd: 'AT+CT,9,255', desc: 'Tuning-Parameter setzen (Index, Wert)' },
      { group: 'Diagnose', cmd: 'AT+E', desc: 'Motortest' },
      { group: 'Diagnose', cmd: 'AT+Q', desc: 'Motor-Plot' },
      { group: 'Diagnose', cmd: 'AT+F', desc: 'Sensortest' },
      { group: 'Diagnose', cmd: 'AT+O', desc: 'Hindernis simulieren' },
      { group: 'Diagnose', cmd: 'AT+O2', desc: 'Regen simulieren' },
      { group: 'Diagnose', cmd: 'AT+O3', desc: 'Niedrigen Akku simulieren' },
      { group: 'Diagnose', cmd: 'AT+G', desc: 'GPS-Lösung umschalten (Test)' },
      { group: 'Diagnose', cmd: 'AT+K', desc: 'Kidnap-Test (Pose auf 0)' },
      { group: 'Diagnose', cmd: 'AT+Z', desc: 'Pathfinder-Stresstest' },
      { group: 'Neustart', cmd: 'AT+Y', desc: 'Mäher neu starten (Watchdog)' },
      { group: 'Neustart', cmd: 'AT+Y2', desc: 'GPS-Empfänger neu starten' },
      { group: 'Neustart', cmd: 'AT+Y3', desc: 'Ausschalten (falls unterstützt)' },
      { group: 'Linux', cmd: 'AT+B1', desc: 'WLAN-Netze suchen' },
      { group: 'Linux', cmd: 'AT+B3', desc: 'Lokale IP-Adresse' },
    ];

    const gpsDashboardEnabled = import.meta.env.VITE_ENABLE_GPS_DASHBOARD === 'true';
    const ubxCommandItems: KnownCommand[] = gpsDashboardEnabled
      ? ubxCommands
          .filter(c => c.hex)
          .map(c => ({ group: 'u-blox (AT+UBX, MowMate-Sunray)', cmd: `AT+UBX,${c.hex}`, desc: `${c.name} (${c.category})` }))
      : [];

    const knownCommands: KnownCommand[] = [...baseCommands, ...ubxCommandItems];

    // Sunray entfernt auf der Konsole das letzte Komma-Feld als Prüfsumme, auch
    // ohne sie zu prüfen: Aus "AT+M,0.1,0" würde "AT+M,0.1". Deshalb bekommen
    // AT-Befehle ohne Prüfsumme sie hier angehängt.
    function withChecksum(cmd: string): string {
      if (!/^AT\+/i.test(cmd) || /,0x[0-9a-f]{1,2}$/i.test(cmd)) return cmd;
      let sum = 0;
      for (let i = 0; i < cmd.length; i++) sum = (sum + cmd.charCodeAt(i)) & 0xff;
      return `${cmd},0x${sum.toString(16).padStart(2, '0')}`;
    }

    function loadHistory(): string[] {
      if (typeof document === 'undefined') return [];
      try {
        const raw = document.cookie.split('; ').find(row => row.startsWith(HISTORY_KEY + '='));
        if (!raw) return [];
        const decoded = decodeURIComponent(raw.substring(HISTORY_KEY.length + 1));
        const parsed = JSON.parse(decoded);
        return Array.isArray(parsed) ? parsed.slice(0, MAX_HISTORY) : [];
      } catch (e) {
        console.warn('[Terminal] could not load history from cookie', e);
        return [];
      }
    }

    function saveHistory(items: string[]) {
      if (typeof document === 'undefined') return;
      try {
        const value = encodeURIComponent(JSON.stringify(items.slice(0, MAX_HISTORY)));
        // 365 days, SameSite lax
        document.cookie = `${HISTORY_KEY}=${value}; path=/; max-age=${60 * 60 * 24 * 365}; SameSite=Lax`;
      } catch (e) {
        console.warn('[Terminal] could not save history to cookie', e);
      }
    }

    function addToHistory(cmd: string) {
      const trimmed = cmd.trim();
      if (!trimmed) return;
      // remove duplicate at front
      history = [trimmed, ...history.filter(h => h !== trimmed)].slice(0, MAX_HISTORY);
      saveHistory(history);
    }

    $effect(() => {
      if (consoleLines != null && consoleLines.length > 0) {
        output = [...output, ...consoleLines];
        let remove = output.length - bufferLines;
        if (remove > 0) {
          output.splice(0, remove);
        }

        if (autoscroll && scrollToIndex != undefined) {
          scrollToIndex(output.length - 1, { behavior: 'smooth' });
        }

        if (onOutputDone) {
          onOutputDone();
        }
      }
    });

  
    function processCommand(event: KeyboardEvent): void {
      if (event.key === 'Enter') {
        const trimmedCommand: string = command.trim();
        if (trimmedCommand) {
          addToHistory(trimmedCommand);
          historyIndex = -1;
  
          executeCommand(trimmedCommand);
  
          command = '';
        }
      } else if (event.key === 'ArrowUp') {
        if (history.length > 0 && historyIndex < history.length - 1) {
          historyIndex++;
          command = history[historyIndex];
          event.preventDefault();
        }
      } else if (event.key === 'ArrowDown') {
        if (historyIndex > 0) {
          historyIndex--;
          command = history[historyIndex];
          event.preventDefault();
        } else if (historyIndex === 0) {
          historyIndex = -1;
          command = '';
          event.preventDefault();
        }
      } else if (event.key === 'ArrowRight' && event.ctrlKey) {
        event.preventDefault();
        openCmdMenuAtCursor();
      }
    }
  
    function executeCommand(cmd: string): void {
      switch (cmd.toLowerCase()) {
        case 'clear':
          output = [];
          return;
        default:
          sendCmd = '';
          sendCmd = withChecksum(cmd);
          break;
      }
    }

    function positionMenu(x: number, y: number) {
      const estimatedHeight = Math.min(window.innerHeight * 0.5, 320);
      const menuWidth = 260;
      menuAbove = y + estimatedHeight > window.innerHeight;
      menuPos = {
        x: Math.min(x, window.innerWidth - menuWidth - 8),
        y: menuAbove ? window.innerHeight - y : y
      };
    }

    function onContextMenu(event: MouseEvent) {
      event.preventDefault();
      positionMenu(event.clientX, event.clientY);
      showCmdMenu = true;
    }

    function openCmdMenuAtCursor() {
      const rect = inputEl?.getBoundingClientRect();
      if (rect) {
        positionMenu(rect.left + 8, rect.top - 8);
      }
      showCmdMenu = true;
    }

    function selectKnownCommand(cmd: string) {
      command = cmd;
      showCmdMenu = false;
      inputEl?.focus();
    }

    function closeMenu(event?: MouseEvent) {
      if (!event || !(event.target instanceof Element) ||
          !event.target.closest('.cmd-menu')) {
        showCmdMenu = false;
      }
    }

    function downloadLog(): void {
      const lines = output.map(l => l.text).join('\n');
      const blob = new Blob([lines], { type: 'text/plain;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `ardumower-log-${new Date().toISOString().replace(/[:.]/g, '-')}.txt`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    }

    export function setFous(el: HTMLElement){
        el.focus()
    }
  </script>
  
  <svelte:window onclick={closeMenu} />

  <main class="terminal-container">
      <div class="terminal-wrapper" style="height:100%;display:flex;flex-direction:column;">
    <div class="terminal-output-virtual">
        <div class="list-holder">
          <VirtualList items={output}
              height="calc(100% - 20px)"
              bind:scrollToIndex
              let:item>
              <div class="console-line">
                  <div class="text">{item.text}</div>
              </div>
          </VirtualList>
        </div>
    </div>
    <div class="terminal-input-wrapper" oncontextmenu={onContextMenu}>
      <span class="prompt">$&nbsp;</span>
      <input
        type="text"
        class="terminal-input"
        bind:value={command}
        onkeydown={processCommand}
        bind:this={inputEl}
        placeholder="AT-Befehl, z. B. AT+S · Rechtsklick oder Strg+→: bekannte Befehle · Prüfsumme wird angehängt"
        spellcheck="false"
        use:setFous
      />
      <div class="terminal-input-actions">
        <span class="autoscroll-label">Autoscroll</span>
        <Toggle
          class="autoscroll-toggle"
          labelText=""
          labelA={""}
          labelB={""}
          bind:toggled={autoscroll}
        />
        <button class="download-btn" onclick={downloadLog} title="Download log as text file">
          Download Log
        </button>
      </div>
    </div>
      </div>
  </main>

  {#if showCmdMenu}
  <div class="cmd-menu" class:cmd-menu-above={menuAbove}
       style="left: {menuPos.x}px; {menuAbove ? 'bottom' : 'top'}: {menuPos.y}px;" role="menu">
    <div class="cmd-menu-title">Bekannte Befehle</div>
    {#each knownCommands as item, i}
      {#if i === 0 || knownCommands[i - 1].group !== item.group}
        <div class="cmd-menu-group">{item.group}</div>
      {/if}
      <button class="cmd-menu-item" onclick={() => selectKnownCommand(item.cmd)} title={item.desc}>
        <span class="cmd-menu-cmd">{item.cmd}</span>
        <span class="cmd-menu-desc">{item.desc}</span>
      </button>
    {/each}
  </div>
  {/if}
  
  <style>
  
    .terminal-container {
      /* füllt den Bereich unter dem Header (das Dashboard zieht ihn ab) */
      height: 100%;
      background-color: #1a1a1a;
      border-radius: 2px;
      box-shadow: 0 4px 10px rgba(0, 0, 0, 0.5);
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }
    
    .terminal-output-virtual {
      flex-grow: 1;
      padding: 10px 15px;
      background-color: #1a1a1a;
      color: #eee;
      white-space: pre-wrap;
      position: relative;

      .list-holder {
        height: 100%;
        position: absolute;
        width: calc(100% - 20px);
      }
    }
    
    .terminal-input-wrapper {
      display: flex;
      align-items: center;
      padding: 10px 15px;
      border-top: 1px solid #333;
      background-color: #1a1a1a;
    }
    .autoscroll-label {
      color: #eee;
      font-size: 0.95em;
      user-select: none;
      display: flex;
      align-items: center;
      height: 100%;
    }
    .terminal-input-actions {
      display: flex;
      align-items: center;
      margin-left: auto;
      gap: 8px;
      margin-top: 0 !important;
      height: 40px;
      min-height: 40px;
    }
    .autoscroll-label {
      color: #eee;
      font-size: 0.95em;
      user-select: none;
      align-self: center;
      line-height: 1;
      height: 32px;
      display: flex;
      align-items: center;
    }
  
    .prompt {
      color: #00ff00;
      margin-right: 5px;
    }
  
    .terminal-input {
      flex-grow: 1;
      background: transparent;
      border: none;
      outline: none;
      color: #eee;
      font-family: inherit;
      font-size: inherit;
      caret-color: #00ff00;
      min-width: 0;
      text-overflow: ellipsis;
    }

    .terminal-input::placeholder {
      color: #6f6f6f;
    }

    .console-line {
        display: flex;
        padding: 3px; 
    }

    .console-line .text {
  line-break: anywhere;
}

.download-btn {
  background: #2a2a2a;
  color: #ccc;
  border: 1px solid #444;
  border-radius: 4px;
  padding: 4px 10px;
  font-size: 0.85em;
  cursor: pointer;
  line-height: 1.4;
  white-space: nowrap;
}
.download-btn:hover {
  background: #3a3a3a;
  color: #fff;
  border-color: #666;
}

.cmd-menu {
  position: fixed;
  z-index: 1000;
  min-width: 260px;
  max-width: 400px;
  max-height: 70vh;
  overflow-y: auto;
  background: #252525;
  border: 1px solid #444;
  border-radius: 4px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.6);
  padding: 6px 0;
  font-size: 0.85em;
}

.cmd-menu-title {
  padding: 6px 12px;
  color: #888;
  border-bottom: 1px solid #383838;
  margin-bottom: 4px;
  font-weight: 600;
}

.cmd-menu-group {
  padding: 8px 12px 2px;
  color: #888;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}

.cmd-menu-item {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  width: 100%;
  padding: 6px 12px;
  background: transparent;
  border: none;
  color: #eee;
  cursor: pointer;
  text-align: left;
}

.cmd-menu-item:hover {
  background: #333;
}

.cmd-menu-cmd {
  font-family: monospace;
  color: #00ff00;
  font-size: 0.95em;
}

.cmd-menu-desc {
  color: #aaa;
  font-size: 0.85em;
  margin-top: 2px;
}

/* Carbon Toggle vertical alignment fix */
:global(.bx--toggle__switch) {
  margin-top: 0 !important;
  margin-bottom: 0 !important;
    }
  </style>
