# Plan: Teilbereiche („Subkarten“) und Mähaufträge

Ziel: Die Task-Funktion von CaSSAndRA nachbilden, aber als Baum von Karten.
Eine Hauptkarte enthält die Geometrie; darunter liegen Subkarten mit eigener
Auswahlfläche, eigenen Mäh-Einstellungen und eigener Route. Mehrere Bereiche
lassen sich zu einem Mähauftrag kombinieren, sofort (Main-Dashboard) oder im
Zeitplaner.

## Grundbegriffe

| | Hauptkarte | Subkarte (Bereich) |
|---|---|---|
| Inhalt | Perimeter, Exclusions, Dockpunkte, Suchdraht, Route über die ganze Fläche | Verweis auf die übergeordnete Karte, Auswahlfläche, eigene Mäh-Einstellungen, eigene Route |
| Geometrie | gehört der Karte | wird geerbt, nicht kopiert |
| Unterebenen | – | Sub-Sub-Karten erlaubt; ihre Auswahl ist nur durch die Karte begrenzt, nicht durch die Auswahl der Elternkarte |

Sunray hält genau eine Karte mit einer Wegpunktliste. Ein **Auftrag** aus
mehreren Bereichen ist deshalb: Geometrie der Hauptkarte plus die Routen der
gewählten Bereiche in der gewählten Reihenfolge, verbunden durch
Verbindungsfahrten um die Exclusions herum, hochgeladen per `AT+W`/`AT+N`.

## Entscheidungen

| Punkt | Entscheidung |
|---|---|
| Hauptkarte als Bereich | bleibt als „ganze Karte“ auswählbar |
| Reihenfolge im Auftrag | vom Nutzer festgelegt (▲/▼), keine Optimierung |
| Überlappende Bereiche | Option je Auftrag: doppelt mähen (Standard) oder nur einmal mähen (Wegpunkte, die in einem früheren Bereich liegen, entfallen; kein Neuplanen) |
| Unterbereiche | Auswahl nur durch Perimeter und Exclusions begrenzt (geändert nach Test: die Begrenzung auf die Elternauswahl ergab keinen Sinn); ein neuer Unterbereich übernimmt Auswahl, Route und Einstellungen der Elternkarte |
| Randrunden einer Subkarte | Standard: nur entlang der echten Außengrenze; Option: auch entlang der Auswahlkante |
| Eigene Exclusions je Subkarte | vorerst nicht |
| Auftrag zu groß für den Mäher | automatisch in Teilaufträge aufteilen (nach freiem Speicher des Mähers, möglichst an Bereichsgrenzen); nach jedem Teil lädt der Mäher, dann startet der nächste |
| Export/Import | wahlweise mit Unterbereichen |
| Speicherung | Subkarte speichert nur Auswahl, Einstellungen und ihre eigene Route |

## Speicherung (Modem)

- Kartenindex `/maps/index.json`: je Eintrag zusätzlich `parentId` (leer bei
  Hauptkarten).
- Eigene Datei je Subkarte mit Auswahlpolygon, Mäh-Einstellungen und
  Wegpunkten, ohne Perimeter, Exclusions, Dockpunkte und Suchdraht.
- Beim Laden einer Subkarte entsteht die „wirksame Karte“: Geometrie der
  Hauptkarte plus Auswahl, Einstellungen und Route der Subkarte. Damit
  funktionieren Anzeige, CRC, Filter der Mow areas, Upload und Berechnung wie
  bei einer Hauptkarte.
- Speichern einer geladenen Subkarte schreibt nur ihren eigenen Teil.
- Löschen einer Karte löscht ihre Unterbereiche mit (nach Rückfrage).
- Ändert sich die Geometrie der Hauptkarte, gelten ihre Subkarten als
  veraltet, bis sie neu berechnet sind.

## Bedienung

- **Kartenauswahl:** das vorhandene Dropdown wird zum Baum. Bei einer Subkarte
  ist die Geometrie der Hauptkarte grau und gesperrt; darüber liegen
  Auswahlfläche, Route und Mäh-Einstellungen der Subkarte. Aktionen „Neuer
  Bereich“, Umbenennen, Löschen.
- **Auswahlwerkzeuge:** Rechteck, Lasso, ganze Elternfläche; die Auswahl
  bleibt als Polygon bearbeitbar.
- **Berechnung:** Elternfläche geschnitten mit der Auswahl, abzüglich
  Exclusions; darauf läuft der vorhandene Planer (alle Muster, Reihenfolge,
  „while passing“). Mäh-Einstellungen werden beim Anlegen von der Elternkarte
  übernommen.
- **Main-Dashboard „Jetzt mähen“:** Mehrfachauswahl der Bereiche der aktiven
  Karte mit Reihenfolge, Option für Überlappungen, Start.
- **Zeitplaner:** ein Eintrag enthält eine geordnete Liste von Bereichen statt
  einer Karte; alte Einträge werden zu einer Liste mit der bisherigen Karte.
- **Fortschritt:** Auftragsroute auf den Dashboards, jeder Bereich in eigener
  Farbe, gemäht grau.

## Etappen

| # | Inhalt | Aufwand |
|---|---|---|
| 1 | Datenmodell, Speicherung, API (anlegen, umbenennen, löschen, Baum auflisten, laden, speichern); bestehende Karten unverändert | ~1 Tag |
| 2 | Baum-Dropdown, Anzeige einer Subkarte mit gesperrter Elterngeometrie | ~1 Tag |
| 3 | Auswahlwerkzeuge Rechteck und Lasso, Auswahl bearbeiten | ~1 Tag |
| 4 | Berechnung auf der Auswahl, Randrunden-Option, „veraltet“-Kennung | ~1 Tag |
| 5 | Aufträge im Modem (Zusammensetzen, Überlappungs-Option, CRC, Upload, Start), Farben je Bereich | ~1,5 Tage |
| (5a) | ggf. Aufteilen zu großer Aufträge in Teilaufträge | ~1 Tag |
| 6 | Main-Dashboard „Jetzt mähen“ mit Mehrfachauswahl | ~0,5 Tag |
| 7 | Zeitplaner mit mehreren Bereichen, Übernahme alter Einträge | ~0,5 Tag |
| 8 | Export/Import mit Unterbereichen, Feinschliff, Release-Notes | ~0,5 Tag |
