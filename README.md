# LedWorm

ESP32-S3-Firmware für eine 16×16-WS2812-Matrix und bis zu acht
WebSocket-Spieler. Jeder verbundene Controller erhält einen eigenen Wurm,
eine feste Farbe und einen verteilten Startpunkt. Der Joystick bewegt den
Spieler auf beiden Achsen; beim Überschreiten eines Matrixrands erscheint
der Wurm auf der gegenüberliegenden Seite. Der verbleibende Bewegungsweg
bleibt dabei erhalten, auch bei diagonalen Übergängen.

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
Spielerplatz freigegeben. Der letzte verbleibende Wurm bildet eine Ausnahme:
Beim Schließen seiner WebSocket-Verbindung bleibt er mit ID, Punkten, Länge,
Körper und beanspruchter Fläche erhalten und wechselt sofort in die automatische
Steuerung. Eine spätere erste Verbindung übernimmt diesen Wurm wieder. Dadurch
bleibt stets mindestens ein aktiver Wurm bestehen. Die Wurmkörper bestehen aus den zuletzt besuchten
LED-Zellen; dahinter bleibt ein ausblendender Schweif. Trifft der Kopf eine
eigene Körperzelle, wird die gerade geschlossene Schleife entfernt. Der
Schnittpunkt wird zum neuen Kopf, das ältere Schwanzstück bleibt erhalten.
Die Länge sinkt um die Zahl der entfernten Segmente, mindestens auf 1;
der Punktestand bleibt unverändert. Abgeschnittene Zellen verschwinden sofort.

Die eingeschlossene Innenfläche bleibt für diese Spielerverbindung in der
Wurmfarbe mit Value 5 eingefärbt. Schleifenränder zählen nicht zur Innenfläche.
Köpfe, Körper und Vitamine werden darüber gezeichnet. Bei überlappenden Flächen
bestimmt der später gezeichnete Spieler die Farbe; die Werte werden nicht addiert.
Kleine Schleifen über Matrixränder werden lokal abgewickelt und korrekt gefüllt.
Eine Schleife, die ganz um die Matrix läuft und keine eindeutige Innenfläche
hat, verkürzt den Wurm ohne Flächenfärbung. Kollisionen mit anderen Würmern
sind noch nicht implementiert.

Bewegt sich der einzige aktive Wurm automatisch in eine neue LED-Zelle, steigt
sein Hue-Wert um 8 und seine Sättigung um 1. Beide Werte sind 8-Bit-Werte und
laufen nach 255 wieder bei 0 weiter. Durchläuft ein Frame mehrere Zellen, wird
die Änderung für jede Zelle angewendet. Bei manueller Steuerung oder mehreren
aktiven Würmern bleibt die aktuelle Farbe unverändert. Die Sättigung wird bei
der HSV-zu-RGB-Umrechnung auch auf Körper und beanspruchte Fläche angewendet.

Spieler werden beim WebSocket-Handshake automatisch angelegt. Die Oberfläche
zeigt die zugewiesene Spieler-ID, Spielerzahl und alle 200 ms den eigenen
Serverzustand: Punktezahl, Länge und Helligkeit (0–255 sowie Prozent).
Die Startlänge beträgt 8. Pro eingesammeltem Vitamin wächst der Wurm um ein
Segment, bis maximal 255. Der Körper baut sich beim Bewegen auf und folgt
auch über Matrixränder dem zurückgelegten Weg. Stillstand fügt keine Segmente
hinzu. Überlappende eigene Segmente belegen dieselbe LED ohne zusätzliche
Helligkeit; dadurch können weniger LEDs als gespeicherte Segmente sichtbar sein.

Der `value-slider` steuert ausschließlich die Helligkeit des eigenen Wurms;
die Zuordnung erfolgt serverseitig über den WebSocket, nicht über eine vom
Browser angegebene Spieler-ID. Standard ist 63 (die bisherige Helligkeit),
0 schaltet Kopf und Körper aus, 255 liefert die volle Kanalhelligkeit. Bereits gezeichnete
Schweife blenden weiterhin aus. Die Hintergrundfarbe bleibt separat steuerbar.
Bei Verbindungsverlust werden veraltete Messwerte ausgeblendet und der Slider
bis zur erneuten Anmeldung deaktiviert.

WebSocket-Nachrichten: `{"type":"value","value":128}` setzt eine ganzzahlige
Helligkeit; `{"type":"state"}` fragt einen unveränderlichen Snapshot ab.
`player_state` liefert `id`, `x`, `y`, `length`, `brightness`, `count`, `score`,
`can_reset_wifi` und `clock_enabled`. Die Position bleibt im Protokoll verfügbar; das GUI zeigt
stattdessen den eigenen Punktestand.
Ein Helligkeitskommando erhält ebenfalls den bestätigten Serverzustand zurück.

Das Controller-GUI besitzt außerdem ein Lauftextfeld. Eine Nachricht der Form
`{"type":"marquee_text","text":"HELLO"}` setzt einen globalen Text mit bis zu
32 druckbaren ASCII-Zeichen; ein leerer Text beendet ihn. Der ESP32 zeichnet
Großbuchstaben, Ziffern und einige Satzzeichen als 5×7-Schrift. Kleinbuchstaben
werden groß dargestellt, unbekannte druckbare Zeichen als Fragezeichen. Der
Text läuft mit einer Spalte pro 120 ms von rechts nach links durch die mittleren
sieben Matrixzeilen und wiederholt sich nach einer kurzen Pause. Er wird in
dezentem Weiß über Würmern, Vitaminen und Flächen gezeichnet.

Die Checkbox **Uhrzeit anzeigen** schaltet stattdessen eine analoge Uhr mit
Stundenmarkierungen sowie Stunden-, Minuten- und Sekundenzeiger ein. Beim
Aktivieren wird ein laufender Text beendet. Jede Eingabe in das Lauftextfeld
deaktiviert die Uhr unmittelbar wieder. Im Heimnetz bezieht LedWorm die Zeit
per SNTP aus dem Internet und rechnet sie nach deutscher Ortszeit samt Sommerzeit
um. Bis zur ersten erfolgreichen Synchronisation bleibt die Uhr unsichtbar;
nach einer Synchronisation läuft sie bei einer Netzunterbrechung weiter. Ein
kalt gestartetes Offline-Spiel besitzt dagegen keine gültige Internetzeit.

## Vitamine und Punkte

Nach drei Sekunden ohne gültige Joysticknachricht steuert der Wurm automatisch
zum aktuellen Vitamin. Er fährt direkt innerhalb der Matrix zum Ziel und
überquert auch beim Ausrollen keine Ränder. Nur die manuelle Bewegung springt
weiterhin auf die gegenüberliegende Seite. Der Wurm
beschleunigt allmählich und bremst schon bei der Annäherung. Nach dem Einsammeln
oder Ablauf des Vitamins verlangsamt er auf eine ruhige Weiterfahrt mit zwei
LED-Zellen pro Sekunde. Er bleibt im Automatikmodus auch ohne Vitamin in
Bewegung und dreht vor den Rändern nach innen. Zum nächsten
Vitamin beschleunigt er wieder langsam; auch Richtungswechsel erfolgen mit
begrenzter Beschleunigung. Jede neue Joysticknachricht (auch die Neutralstellung) gibt
die Steuerung sofort an den Spieler zurück und startet dessen Timer neu.
Helligkeitsänderungen, Zustandsabfragen und ungültige Eingaben zählen nicht
als Joystickaktivität. Die Timer gelten pro Spieler und starten bei einer
neuen Verbindung neu. Die Automatik vermeidet keine eigenen Körpersegmente;
die Regeln für Selbstschnitte gelten weiterhin.

Solange Spieler verbunden sind, erscheint nach fünf Sekunden ein weißes Vitamin
auf einer zufälligen LED, die von keinem Kopf oder Körper belegt ist. Es bleibt bis zu acht
Sekunden aktiv. Es blendet über vier Sekunden sanft aus der darunterliegenden
Farbe zu dezentem Weiß (Value 48 statt zuvor 180) und über vier Sekunden wieder
zurück. Die Übergänge laufen zeitbasiert mit weichem Start und Ende.
Erreicht ein Wurmkopf diese LED, verschwindet das Vitamin und
der Spieler erhält einen Punkt und ein zusätzliches Körpersegment (bis Länge 255).
Treffen mehrere Köpfe im selben Frame ein,
bekommt der erste aktive Spieler in der internen Slot-Reihenfolge den Punkt.
Nach Einsammeln oder Ablauf erscheint nach drei Sekunden das nächste Vitamin.
Es ist immer höchstens eines aktiv; Vitamine hinterlassen keinen Schweif.

Die Punkte werden unter demselben Mutex wie die Bewegung auf dem ESP32 verwaltet.
Sie gelten pro Spielerverbindung und starten bei einer neuen Anmeldung bei null.
Ohne Spieler werden keine Vitamine angezeigt. Die Regeln gelten sowohl im
Heimnetz als auch beim Offline-Spiel.

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
WLAN-Zugangsdaten. Über **WLAN-Auswahl** im Offline-Controller geht es direkt
zurück ins Setup. Dabei werden die Spieler getrennt, das Setup-WLAN bleibt
aktiv und gespeicherte Zugangsdaten bleiben erhalten.
Nach einem Neustart gilt wieder der normale Ablauf:
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
**WLAN zurücksetzen**. Im Offline-Spiel heißt sie **WLAN-Auswahl** und öffnet
stattdessen das Setup; der Reset-Endpunkt lehnt dort Aufrufe ab. Nach Bestätigung
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
Punkte werden beim Einsammeln aktualisiert, und nach einer Trennung
erscheinen bis zum erneuten Verbindungsaufbau keine alten Werte.

mDNS am Gerät prüfen: Nach dem WLAN-Start `http://ledworm.local/` auf einem
zweiten Gerät im selben Heimnetz öffnen und den Controller benutzen. Nach
Neustart sowie kurzem Router-Ausfall erneut prüfen, auch bei geänderter DHCP-IP.
Bei längerem Ausfall muss das Setup weiterhin über 192.168.4.1 erreichbar sein.
