# ESPHome-Geräte

Antworten auf Deutsch (Schweizer Schreibweise, kein ß).

Die Aquarium-Geräte hängen eng an Home Assistant. Das Wissen zur HA-Seite (Entitäten, Skripte, Einstellungen, Sync) kommt aus dem HA-Repo:

@~/projects/Homeassistant/homeassistant/CLAUDE.md

## Arbeitsweise

- **Commits:** direkt auf `main`, nur auf Wunsch, mit dem vorherigen Commit als Rollback-Stand. Vorher `git pull`.
- **Flashen:** Marc flasht über das ESPHome-Dashboard (Device Builder), nicht von hier.
- **Prüfen:** ESPHome ist lokal nicht installiert. Bei Bedarf in einem venv mit Python 3.13 installieren (`pip install esphome`). `secrets.yaml` liegt nicht im Repo; zum Prüfen die YAML mit `fonts/` und `pictures/` in einen Testordner kopieren und dort Platzhalter-Secrets anlegen. Für API-Schlüssel braucht es einen gültigen, nicht leeren Schlüssel aus `openssl rand -base64 32`. Dann `esphome config` und für Änderungen an Lambdas `esphome compile` ausführen.
- **Ändert sich etwas an HA-Entitäten**, die ein Gerät liest (siehe HA-`CLAUDE.md`), das Gerät gleich mit anpassen.

## Aquarium Monitor (`aquarium-monitor.yaml`)

- **Hardware:** Seeed XIAO ESP32-C6, Hostname `aquarium-monitor` (früher `sht41`).
- **Pins:**

| Pin | GPIO | Sensor |
|---|---|---|
| D0 | GPIO0 | TDS-Sonde (ADC) |
| D3 | GPIO21 | DS18B20 Wassertemperatur (1-Wire, 4,7 kΩ Pull-up nach 3V3) |
| D6 / D7 | GPIO16 / 17 | I2C SDA / SCL: SHT41 Raumklima (0x44) |

- **Wassertemperatur:** kalibriert am 30.10.2026 mit Eisbad und Siedepunkt (Zürich, ca. 410 m, 98,6 °C) über `calibrate_linear: -2.9 -> 0.0, 95.3 -> 98.6`. Der Fühler misst roh etwa 3 °C zu tief.
- **TDS:**
  - Temperaturkompensation auf 25 °C, Faktor k = 1,0 (geprüft mit Zürcher Leitungswasser, ca. 170 ppm erwartet, 187 gemessen).
  - Ziel ist das Erkennen von Veränderungen, nicht absolute Genauigkeit.
- **Trend-Sensoren:**
  - `TDS Rise Since Water Change` (%): Ausgangswert ist das 5-Minuten-Mittel 30 Minuten nach einem Wasserwechsel. Der Wasserwechsel kommt aus `sensor.aquarium_wasserwechsel_zeitpunkt` in HA; Einträge, die älter als 24 h sind, löschen den Ausgangswert.
  - `TDS Jump`: Abweichung zwischen 5-Minuten- und 1-Stunden-Mittel über 10 %.
  - `Water Change Recommended`: Anstieg über 25 %.

## Aquarium Tablet (`aqarium-tablet.yaml`, Gerät `aquarium-display`)

### Zwei Becken
- **Becken 0** = HA-Becken `aquarium` (Nano Cube Garnelen), **Becken 1** = `aquarium1` (Nano Cube Plants). Der Global `tank` (0/1) ist das aktive Becken.
- **Daten pro Becken:** `common/aquarium-tablet-becken.yaml` wird über `packages` zweimal eingebunden (`vars: s` = Entitäts-Suffix `""`/`"1"`, `n` = Index). Die IDs heissen `ha_<x>` (Becken 0) und `ha1_<x>` (Becken 1).
- **In Lambdas immer das aktive Becken lesen**, mit `(id(tank) ? id(ha1_x) : id(ha_x))->state`. ESPHome wandelt nur `id(x).` in `x->` um; nach einem Ausdruck in Klammern muss deshalb `->` stehen. Einzeilige Lambdas mit `? … : …` als YAML-Block (`|-`) schreiben, sonst liest YAML das `: ` als Schlüssel.
- **Hardware** (Licht, CO2, Pumpe, Leistung, Monitor; die Substitutions oben) gehört vorerst nur zu Becken 0. `refresh_hw` zeigt bei Becken 1 „--“, und die Schalter sind gesperrt. Die Hardware von Becken 1 wird später eingebunden.
- **Speichern** ruft je nach `tank` `script.aquarium_…_vom_tablet` oder `script.aquarium1_…_vom_tablet` auf.
- **Startseite** `page_start` mit einem Button pro Becken (`open_tank`). Kopfzeile: auf der Startseite der Titel, im Becken der Zurück-Knopf mit Kurzname (`hdr_tank`) und die Tabs (`hdr_nav`). Nach dem Aufwachen zeigt das Tablet immer die Startseite (`show_start` in `on_turn_on` der Hintergrundbeleuchtung).
- **Status** (Übersicht und Startseite): TDS-Sprung bzw. TDS zu hoch (nur Becken 0) > Warnungen aus HA (`sensor.aquarium<s>_warnungen`, Text = Attribut `erste`, dazu „(+n)“) > Werte ausser Soll > „Alles in Ordnung“.

### Allgemein

- **Hardware:** Guition 4848S040 (ESP32-S3, 480×480), `display: model: GUITION-4848S040`. Das generische ST7701S-Profil führt zu einem unscharfen, flackernden Bild.
- **Design:** LVGL, Dark Mode mit wenigen Farbakzenten (Gelb `0xD9A920` = an/aktiv, Orange `0xFF7A3D` = Warnung, Rot = TDS-Sprung, Grün = alles in Ordnung).
- **Tabs:**
  - **Übersicht:** Sensoren, Geräte, Leistung, Wasserwechsel, nächste Zugabe.
  - **Steuerung:** Licht, CO2, Pumpe.
  - **Wasser** mit zwei Unter-Tabs:
    - *Werte:* Eingabe der Tropfentests.
    - *Wasserwechsel:* Menge in Liter; vorbelegt mit `input_number.aq_ww_liter`, Schnellwahl 3/5/7.5/10/15/20 L.
- **Display-Schlaf:**
  - Aus nach 3 Minuten ohne Berührung und ohne Bewegung (`binary_sensor.bewegungsmelder_aqara_occupancy`).
  - Bewegung weckt es von 07:00 bis 23:00, Berührung weckt es immer. Die Weck-Berührung löst keinen Button aus.
  - Einbrennschutz um 02:05, 05:05 und 08:05, nur wenn das Display schläft.
- **Schriften:** `inter_56` und die Zahlen-Schriften enthalten nur die Zeichen `0123456789.,+-%`. Andere Zeichen dort nicht verwenden.
