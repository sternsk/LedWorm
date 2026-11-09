# LedWorm

ESP32-S3-Firmware für eine 16×16-WS2812-Matrix und bis zu acht
WebSocket-Spieler. Jeder verbundene Controller erhält einen eigenen Wurm,
eine feste Farbe und einen verteilten Startpunkt. Der Joystick bewegt den
Spieler auf beiden Achsen; an den Matrixrändern stoppt die Bewegung.

## Matrix und Rendering

In `src/globals.h` sind Abmessungen, LED-Pin und Verdrahtung festgelegt:

- `MATRIX_SERPENTINE = false`: zeilenweise, LED-Index `y * 16 + x`.
- `MATRIX_SERPENTINE = true`: jede zweite Zeile läuft rückwärts.
- Der Ursprung `(0, 0)` ist links unten; positives Y bewegt nach oben.
  Die physische Orientierung muss entsprechend zur Verdrahtung passen.

Der Render-Task aktualisiert alle Spieler mit demselben Zeitstempel und
übernimmt ihre Positionen unter einem Mutex. Die LED-Ausgabe erfolgt danach
ohne diesen Lock. Pro Frame werden alle Köpfe zusammengeführt und einmal an
die Hardware gesendet. Überlappende Köpfe mischen ihre Farben; alte Positionen
blenden zur gewählten Hintergrundfarbe aus. Beim Verbindungsende wird der
Spielerplatz freigegeben. Die Schweife sind ein visueller Fade, keine bereits
implementierten Spielkörper oder Kollisionen.

Spieler werden beim WebSocket-Handshake automatisch angelegt. Die Oberfläche
zeigt die zugewiesene Spieler-ID, Spielerzahl und alle 200 ms den eigenen
Serverzustand: Position X/Y, konfigurierte Länge und Helligkeit (0–255 sowie
Prozent). Die konfigurierte Länge beträgt aktuell 8; der sichtbare Schweif ist
weiterhin ein Fade und kein Segmentkörper dieser Länge.

Der `value-slider` steuert ausschließlich die Helligkeit des eigenen Wurms;
die Zuordnung erfolgt serverseitig über den WebSocket, nicht über eine vom
Browser angegebene Spieler-ID. Standard ist 63 (die bisherige Helligkeit),
0 schaltet den Kopf aus, 255 liefert die volle Kanalhelligkeit. Bereits gezeichnete
Schweife blenden weiterhin aus. Die Hintergrundfarbe bleibt separat steuerbar.
Bei Verbindungsverlust werden veraltete Messwerte ausgeblendet und der Slider
bis zur erneuten Anmeldung deaktiviert.

WebSocket-Nachrichten: `{"type":"value","value":128}` setzt eine ganzzahlige
Helligkeit; `{"type":"state"}` fragt einen unveränderlichen Snapshot ab.
`player_state` liefert `id`, `x`, `y`, `length`, `brightness` und `count`.
Ein Helligkeitskommando erhält ebenfalls den bestätigten Serverzustand zurück.

## Build und Tests

Die in `platformio.ini` gepinnte pioarduino-Plattform benötigt PlatformIO
Core mindestens 6.1.19. Firmware bauen:

```sh
pio run -e LedWorm -j 4
```

Für acht Spieler sind `CONFIG_LWIP_MAX_SOCKETS=16` in `sdkconfig.LedWorm`
und `sdkconfig.defaults` hinterlegt. Der HTTP-Server erlaubt zehn
Client-Sockets einschließlich bis zu acht Spieler-WebSockets im Heimnetz.

Hardwareunabhängige Regressionstests mit G++, AddressSanitizer und
UndefinedBehaviorSanitizer:

```sh
bash tests/run_host_tests.sh
```

Die Tests prüfen unabhängige Bewegung, Matrixgrenzen, acht Spieler,
Slot-Wiederverwendung, ungültige Eingaben, parallele Joystick-/Render-Zugriffe,
beide Verdrahtungsvarianten, Farbmischung, individuelle Helligkeit einschließlich
0/255, Zustandsabfragen und ausblendende Schweife. Der
FreeRTOS-Mutex wird dabei durch einen echten Host-Mutex ersetzt. Die Tests
ersetzen keinen Firmware-Build oder Funktionstest am ESP32.

## WLAN einrichten

### Offline spielen

Im WLAN `LedWorm-Setup` unter **http://192.168.4.1/** die Schaltfläche
**Offline spielen** wählen. Die Seite öffnet automatisch den Controller.
LedWorm bleibt als offener Access Point aktiv; bis zu acht Geräte können
sich direkt verbinden und unter **http://192.168.4.1/** spielen. Ein Router
oder Internetzugang ist nicht nötig. Falls das Bediengerät wegen fehlendem
Internetzugang nachfragt, die Verbindung zu diesem WLAN beibehalten.

Die Auswahl gilt bis zum Neustart und verändert keine gespeicherten
WLAN-Zugangsdaten. Nach einem Neustart gilt wieder der normale Ablauf:
Verbindung zum gespeicherten WLAN oder Setup. Ohne gespeicherte Daten
kann im Setup erneut **Offline spielen** gewählt werden.

### Verbindung mit einem WLAN

Ohne gespeicherte Zugangsdaten bietet der ESP32 das offene WLAN
`LedWorm-Setup` an. Damit verbinden und **http://192.168.4.1/** öffnen
(kein automatisches Captive Portal). Dort WLANs suchen oder die SSID manuell
eingeben, Passwort eintragen und speichern. Unterstützt werden 2,4-GHz-Netze
mit offenem Zugang oder persönlichem WLAN-Passwort (8–63 Bytes); Enterprise-
Anmeldung und 64-stellige Hex-PSKs sind nicht Teil dieser Oberfläche.
SSID: 1–32 Bytes, einschließlich Leerzeichen und UTF-8-Zeichen.

Nach erfolgreichem Speichern in NVS startet LedWorm neu. Das Setup-WLAN
verschwindet. Das Bediengerät wieder mit dem Heimnetz verbinden und die
**http://ledworm.local/** öffnen. Nach IP-Bezug meldet LedWorm den Hostnamen
`ledworm.local` und den HTTP-Dienst `_http._tcp` auf Port 80 per mDNS an.
Der WebSocket verwendet automatisch denselben Hostnamen. Zusätzlich wird
`ledworm` als DHCP-Hostname gesetzt. Beim Wechsel zurück ins Setup wird mDNS
beendet; nach kurzen WLAN-Unterbrechungen übernimmt mDNS die aktuelle IP.

Falls das Betriebssystem oder das WLAN kein mDNS unterstützt (z. B. durch
Client-Isolation oder gefilterten Multicast), die ESP32-IP im Router oder im
seriellen Monitor nachsehen und `http://<ESP32-IP>/` öffnen. Beide Geräte
müssen sich im selben lokalen Netz befinden. `localhost` bezeichnet den
eigenen PC. Im Setup gilt weiterhin `http://192.168.4.1/`.
Mehrere LedWorm-Geräte benötigen unterschiedliche mDNS-Namen; der hier
konfigurierte Name ist fest `ledworm`.

Gespeicherte Zugangsdaten werden bei jedem Start verwendet. Wird innerhalb
von etwa 30 Sekunden keine IP bezogen, öffnet sich wieder `LedWorm-Setup`.
Dasselbe gilt bei längerem Verbindungsverlust während des Spiels. Bei kurzen
Ausfällen verbindet sich der ESP32 erneut; Browser müssen gegebenenfalls neu
verbunden werden. Beim Wechsel ins Setup werden der Spielserver und seine
WebSockets geschlossen. Bestehende Zugangsdaten bleiben bis zum Überschreiben
oder expliziten Zurücksetzen gespeichert.

`wifi_manager.cpp` besitzt Netzwerk, NVS und einen eigenen Setup-HTTP-Server.
Ein separater FreeRTOS-Task verwaltet Verbindungsversuche und Moduswechsel.
Event-Handler melden nur den Verbindungsstatus. `main.cpp` startet den
Spielserver erst nach IP-Bezug und stoppt ihn vor dem Setup-Server; beide
verwenden exklusiv Port 80. Der LED-Task läuft auch während des Setups weiter.
Die WLAN-Suche blockiert ausschließlich den separaten Setup-HTTP-Task.

Im Controller gibt es beim Spielen über das Heimnetz die Schaltfläche
**WLAN zurücksetzen**. Im Offline-Spiel ist sie ausgeblendet und der
Reset-Endpunkt lehnt Aufrufe ab. Nach Bestätigung
werden die gespeicherten WLAN-Zugangsdaten gelöscht und LedWorm startet neu;
alle Spieler werden getrennt. Danach mit `LedWorm-Setup` verbinden und
**http://192.168.4.1/** öffnen, um das WLAN erneut einzurichten. Andere NVS-Daten
bleiben erhalten. Der Aufruf erfolgt über `POST /api/wifi/reset` am Spielserver.

`wifi_manager_reset()` löscht nur den Schlüssel im Namespace `ledworm_wifi`
und plant einen Neustart. Die Funktion kann später aus einem entprellten
Tasten-Task nach langem Tastendruck aufgerufen werden, nicht aus einem ISR.
Die Tastenbelegung ist noch nicht implementiert. NVS-Initialisierungsfehler
führen bewusst nicht zum automatischen Löschen anderer Anwendungsdaten.

Das Setup-WLAN und sein HTTP-Zugang sind offen; die Einrichtung in einer
vertrauenswürdigen Umgebung durchführen. Zugangsdaten werden nicht geloggt,
liegen aber ohne aktivierte NVS-/Flash-Verschlüsselung unverschlüsselt im Flash.

Zusätzlicher Oberflächentest (Node.js, ohne Zusatzpakete):

```sh
node tests/wifi_setup_test.cjs
```

### Prüfung am ESP32

1. Ohne WLAN-Konfiguration starten: Setup-WLAN und Seite unter 192.168.4.1
   prüfen; LED-Rendering muss weiterlaufen.
2. WLAN-Suche und manuelle SSID prüfen, gültige Daten speichern: Neustart,
   `http://ledworm.local/`, Router-IP und Spiel mit mehreren WebSockets prüfen.
3. Strom trennen und neu starten: automatische Verbindung mit gespeicherten Daten.
4. Falsches Passwort speichern: nach etwa 30 Sekunden wieder Setup, anschließend
   mit richtigen Daten korrigieren. Auch versteckte und offene Netze prüfen.
5. Router kurz und länger als 30 Sekunden ausschalten: Wiederverbindung bzw.
   Rückkehr ins Setup und Freigabe der Spielerplätze prüfen.
6. `wifi_manager_reset()` aus einem Task auslösen: nach Neustart wieder Setup;
   andere NVS-Anwendungsdaten müssen erhalten bleiben.

Hosttests und Firmware-Build ersetzen diese Funk-/Hardwareprüfungen nicht.

Controller-Oberfläche ohne Zusatzpakete testen:

```sh
node tests/controller_test.cjs
```

Am Gerät mit zwei Browsern prüfen: nur der eigene Wurm ändert seine Helligkeit,
Positionen werden während der Bewegung aktualisiert, und nach einer Trennung
erscheinen bis zum erneuten Verbindungsaufbau keine alten Werte.

mDNS am Gerät prüfen: Nach dem WLAN-Start `http://ledworm.local/` auf einem
zweiten Gerät im selben Heimnetz öffnen und den Controller benutzen. Nach
Neustart sowie kurzem Router-Ausfall erneut prüfen, auch bei geänderter DHCP-IP.
Bei längerem Ausfall muss das Setup weiterhin über 192.168.4.1 erreichbar sein.
