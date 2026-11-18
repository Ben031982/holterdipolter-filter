# holterdipolter Filter

Parametrischer 24-Band-Equalizer als VST3, gebaut mit JUCE 9. Version 1.0.

## Was drin ist

- Bis zu 24 Bänder, in Reihe, ohne Latenz
- Zehn Filterformen: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, All Pass, Flat Tilt
- Flankensteilheit 6 bis 96 dB/oct
- Pro Band wählbar: Stereo, Left, Right, Mid oder Side
- Dynamic EQ: Bell und Shelves können auf den Pegel reagieren, auf Wunsch auf den der Sidechain-Spur
- EQ Sketch: Wunschkurve zeichnen, die Bänder dazu entstehen von selbst
- Referenzkurve und EQ Match: eine Spur klanglich an eine andere angleichen
- Band-Solo: nur den Bereich hören, in dem ein Band arbeitet
- Auto Gain: gleicht die Pegeländerung durch die EQ-Kurve aus
- Undo und Redo
- Presets als Dateien, 18 Factory-Presets, A/B-Vergleich
- Mehrere Bänder auswählen, gemeinsam bewegen, kopieren und einfügen (auch zwischen Instanzen)
- Spectrum Analyzer vor und nach dem EQ, mit Freeze, Tempo, Auflösung, Tilt und Pegelbereich
- Spectrum Grab: Spitzen im Analyzer anfassen und als Band herunterziehen
- Sidechain-Eingang: Spektrum einer zweiten Spur im Analyzer, mit Kollisionsanzeige
- Klaviatur unter dem Display, Notennamen statt Hertz eintippbar
- Gain Scale (0 bis 200 %, skaliert alle Band-Gains gemeinsam), Output Gain, Phasenumkehr
- Mono und Stereo, alle Parameter automatisierbar, Fenstergröße frei ziehbar

## Neu in 1.0

- **EQ Match.** Analyzer-Menü, "EQ Match: make this track sound like the reference". Voraussetzung ist eine
  Referenzkurve (siehe 0.9). Das Plugin hört vier Sekunden auf das, was den EQ verlässt, vergleicht es mit der
  Referenz und legt bis zu acht Bänder an, die den Unterschied im Klang ausgleichen.
  - Es zählt nur die Klangfarbe, nicht die Lautstärke: der mittlere Pegelunterschied wird herausgerechnet.
  - Höchstens 12 dB pro Stelle, geglättet auf etwa eine Terz, in den Tiefen breiter. Unterschiede unter
    1,5 dB bekommen kein Band. Wo die Spur kein Signal hat, wird nichts verlangt.
  - Die neuen Bänder sind gewählt; GAIN SCALE dosiert das Ergebnis. Ein Undo nimmt den ganzen Match zurück.
- Während einer Aufnahme (Referenz oder Match) hält Spectrum Grab den Analyzer nicht mehr an.
- **Userguide** auf 1.0 gebracht.

## Neu in 0.9

- **Kollision per Klick beheben.** Auf jedem roten Bereich sitzt oben ein Schild FIX. Ein Klick legt dort eine
  Glocke an: so breit wie der Bereich, 0 dB in Ruhe, Range -6 dB, SC an. Das Band gibt also nur nach, solange
  die Sidechain-Spur klingt. Das Rot klingt jetzt etwa eine Sekunde nach, damit es nicht im Takt flackert.
- **Eigene Voreinstellungen.** Analyzer-Menü, "Save this view as my default": Klaviatur, Analyzer-Optionen,
  Display-Bereich und Fenstergröße gelten ab dann für jede neue Instanz. Der EQ selbst startet leer.
  Bestehende Projekte behalten ihre Ansicht. "Back to the built-in default" nimmt es zurück.
- **Referenzkurve.** Analyzer-Menü, "Record 4 seconds of this track" oder "... of the sidechain": Das Plugin
  mittelt vier Sekunden lang das Spektrum und lässt es als gestrichelte Linie stehen. Die Kurve wird mit dem
  Projekt gespeichert und folgt dem Tilt der Anzeige.
- **Preset-Ordner.** Unterordner im Preset-Verzeichnis erscheinen als Untermenüs. Beim Speichern sortiert
  `Kick/Mein Preset` das Preset in den Ordner Kick und legt ihn bei Bedarf an. Bis zu drei Ebenen.

## Neu in 0.6

- **Dynamik hört auf den Sidechain.** In der Dynamik-Zeile des Bandstreifens steht neben AUTO der Schalter
  **SC**. Ist er an, reagiert das Band auf die Spur am Sidechain-Eingang statt auf die eigene. Beispiel: Band
  auf dem Bass bei 60 Hz, Range -9 dB, SC an, Kick als Sidechain. Der Bass gibt bei 60 Hz nur nach, solange
  die Kick klingt. Liegt kein Sidechain an, ruht das Band.
- **Kollisionsanzeige mit neuer Regel.** Rot zeigt jetzt, wo die eigene Spur der anderen im Weg ist:
  gleichauf oder lauter ist volles Rot, je weiter die eigene Spur darunter liegt, desto blasser, ab 12 dB
  Abstand ist es weg. Gemessen wird immer hinter dem EQ, eine Absenkung lässt das Rot also verschwinden.
- **EQ Sketch.** Knopf SKETCH im Display, dann mit gedrückter Maustaste die gewünschte Kurve ziehen. Beim
  Loslassen legt das Plugin bis zu sechs Bänder an, die ihr folgen: Bell für Buckel und Senken, Shelf, wo die
  Linie bis zum Rand oben oder unten bleibt. Kleine Wackler der Hand bekommen kein eigenes Band. Ein Undo
  nimmt die ganze Zeichnung zurück, Esc bricht ab.
- **Userguide** als PDF neu geschrieben.

## Neu in 0.5

- **Spectrum Grab.** Ruht die Maus gut eine halbe Sekunde auf einer leeren Stelle des Analyzers, bleibt das
  Spektrum stehen und seine auffälligsten Spitzen werden markiert und beschriftet (mit Klaviatur auch mit
  Note). Eine Markierung anfassen und nach unten ziehen legt genau dort ein Band an, so breit wie die Spitze.
  Maus wegbewegen, loslassen oder Esc gibt das Spektrum wieder frei. Abschaltbar im Analyzer-Menü.
- **Externes Spektrum.** Das Plugin hat einen Sidechain-Eingang. Was dort ankommt, zeichnet der Analyzer als
  orange Linie; zu hören ist es nie. In Ableton: im Gerät den Sidechain aufklappen und bei "Audio From" die
  andere Spur wählen.
- **Kollisionsanzeige.** Wo die eigene Spur und der Sidechain gleichzeitig laut sind, färbt sich das Display
  rot. Als laut zählt, was höchstens 24 dB unter der stärksten Spitze im Bild liegt; ab 12 dB darunter ist
  die Markierung voll. Die Anzeige bewegt sich mit der Musik. Beides im Analyzer-Menü schaltbar.
- **Factory-Presets.** 18 Ausgangspunkte in fünf Gruppen (Aufräumen, Kick und Bass, Höhen, Summe und Effekt,
  Schranz), fest im Plugin. Sie lassen sich nicht überschreiben oder löschen; "Save as" macht aus einem
  veränderten Factory-Preset ein eigenes. Die Pfeile blättern erst durch die Factory-Presets, dann durch die
  eigenen.

## Neu in 0.4

- **Dynamic EQ.** Bell, Low Shelf und High Shelf haben in der zweiten Zeile des Bandstreifens einen
  Dynamikbereich (RANGE). Steht er auf 0, ist das Band wie bisher. Sonst wird der Bereich zum Gain addiert,
  sobald das Signal dort laut wird, wo das Band arbeitet: Gain 0 dB und Range -6 dB senkt eine Resonanz nur
  ab, wenn sie hervortritt; eine positive Range hebt an, was laut ist.
  - **Threshold:** ab diesem Pegel beginnt das Band zu arbeiten, 12 dB darüber ist der volle Bereich erreicht.
  - **AUTO** (Voreinstellung): der Threshold richtet sich nach dem Gesamtpegel. Das Band reagiert, wenn sein
    Bereich weniger als 12 dB unter dem ganzen Signal liegt, egal wie laut die Spur ist. Das ist eine eigene
    Festlegung, nicht die von FabFilter.
  - Das Display zeigt an jedem dynamischen Punkt einen Strich bis zum Ende des Bereichs; der dicke Teil ist,
    was gerade wirkt. Rechts in der Zeile steht derselbe Wert in dB.
  - Alt + Mausrad über einem Punkt stellt den Bereich ein; im Rechtsklick-Menü schaltet "Dynamic" ein und aus.
  - Regelzeiten sind fest: rund 20 ms bis zum Eingreifen, 100 ms Rücklauf. Das Band hört auf das Eingangssignal
    des Plugins, bei Mid, Side, Left und Right nur auf diesen Teil. Auto Gain gleicht die Dynamik nicht aus.
- **Klaviatur beschriftet.** Unter jedem C steht sein Name (C4 ist das mittlere C). Ist das Fenster breit genug,
  trägt jede weiße Taste ihren Buchstaben. Ein Schild über den Tasten nennt die Note unter der Maus und die
  Note jedes Bands, das gewählt ist oder gerade bewegt wird. Schreibweise wie in Ableton: B, nicht H.

## Neu in 0.3

- **Mehrfachauswahl.** Rahmen über leere Fläche ziehen oder Strg-Klick auf Punkte. Zieht man einen der
  gewählten Punkte, gehen alle mit: gleiches Intervall, gleiche dB. Mausrad, Entf und das Rechtsklick-Menü
  wirken auf die ganze Auswahl. Der Bandstreifen unten bearbeitet das Band, das zuletzt angefasst wurde.
- **Kopieren und Einfügen** mit Strg+C, Strg+X, Strg+V oder Rechtsklick. Die Bänder liegen als Text in der
  Zwischenablage und lassen sich deshalb auch in eine andere Instanz einfügen.
- **Presets.** Ein Preset ist eine Datei in `Dokumente\holterdipolter\Filter\Presets`. Klick auf das
  Preset-Feld öffnet das Menü, die Pfeile blättern. Ein Stern hinter dem Namen heißt: seit dem Laden verändert.
  Laden lässt sich mit Undo zurücknehmen.
- **A/B.** Zwei Einstellungen nebeneinander. B beginnt als Kopie von A. Der Knopf daneben kopiert die
  hörbare Einstellung auf die andere Seite. Beide Seiten werden mit dem Projekt gespeichert.
- **Analyzer-Menü:** Freeze, Tempo, Auflösung, Tilt (0 bis 6 dB/oct) und Pegelbereich (60 bis 120 dB).
- **Display-Bereich ±3 dB** für feine Korrekturen.
- **Flat Tilt:** eine gerade Schräge über das ganze Spektrum. Gain ist der Unterschied zwischen 20 Hz und
  20 kHz, Frequenz der Punkt, an dem die Linie durch 0 dB geht.
- **Phasenumkehr** am Ausgang.
- **Klaviatur** (Knopf PIANO): zeigt, auf welcher Note jedes Band sitzt. Solange sie sichtbar ist, rastet
  Ziehen auf Halbtöne; mit Shift frei. Klick auf eine Taste setzt das gewählte Band auf diese Note. In das
  Frequenzfeld lässt sich immer auch eine Note tippen, etwa `A1` oder `C#3`.

Wer mit 0.2 die Form eines Bands in der DAW automatisiert hat: Durch die zehnte Form verschieben sich die
automatisierten Werte dieses einen Parameters. Gespeicherte Projekte und alles andere bleiben, wie sie waren.

## Neu in 0.2

- **Filterform bis zur halben Abtastrate.** Glocken, Shelves, High Cuts und Band Pässe folgen jetzt der
  analogen Kurve, statt nahe der halben Abtastrate schmaler zu werden. Gemessen bei 44,1 und 48 kHz, Bänder
  zwischen 6 und 15 kHz, übliche Einstellungen: mittlere Abweichung einer Glocke 0,35 dB (vorher 5,6 dB),
  eines High Shelf 0,3 dB (vorher 1,5 dB).
- **Kein Knacken mehr** beim Wechsel von Form, Flanke, Stereo-Platzierung oder Bypass: alter und neuer Filter
  werden 12 ms lang überblendet.
- **Band-Solo, Auto Gain, Undo/Redo.**

## Bauen über GitHub Actions

1. Jeder Push auf `main` startet unter **Actions** den Workflow „Build VST3" für Windows. Änderungen nur an
   Texten (`*.md`, `docs/`) lösen keinen Build aus.
2. Nach dem Lauf die Datei unter **Artifacts** herunterladen: `holterdipolter-Filter-Windows-VST3.zip`.
3. macOS wird nur gebaut, wenn der Workflow von Hand gestartet wird („Run workflow", Haken bei
   „Also build for macOS"). Mac-Rechner sind bei GitHub knapp und kosten bei privaten Repositories das
   Zehnfache an Actions-Minuten.

**Release:** Ein Tag wie `v1.1` baut Windows und legt unter **Releases** eine Version mit der Plugin-ZIP und dem
Userguide an. Der Text dazu ist der Abschnitt „## Neu in 1.1" aus diesem README; den also vorher schreiben.

Der Workflow baut auch die DSP-Tests und bricht ab, wenn einer fehlschlägt. Jeder Lauf baut JUCE mit
und dauert deshalb einige Minuten.

Der Userguide liegt als PDF in `docs/`.

## Installieren

Das entpackte `holterdipolter Filter.vst3` als Ganzes kopieren (es ist ein Ordner, kein einzelnes File):

- **Windows:** `C:\Program Files\Common Files\VST3\`
- **macOS:** `~/Library/Audio/Plug-Ins/VST3/`

Danach in der DAW die Plugins neu scannen.

Auf macOS ist das Plugin nicht notarisiert. Nach dem Kopieren einmal im Terminal:

```
xattr -cr ~/Library/Audio/Plug-Ins/VST3/"holterdipolter Filter.vst3"
```

## Bedienung

| Aktion | Wirkung |
| --- | --- |
| Doppelklick ins Display | Band anlegen (ganz links: Low Cut, ganz rechts: High Cut) |
| Punkt ziehen | Frequenz und Gain; bei Cut, Notch, Band Pass, All Pass ändert hoch/runter das Q |
| Shift beim Ziehen | fein einstellen |
| Mausrad über einem Punkt | Q |
| Alt + Mausrad über einem Punkt | Dynamikbereich |
| Alt-Klick auf einen Punkt | Band stummschalten |
| Rechtsklick auf einen Punkt | Menü: Form, Flanke, Stereo-Platzierung, Solo, kopieren, entfernen |
| Rahmen über leere Fläche ziehen, Strg-Klick | mehrere Bänder auswählen |
| Strg+A, Strg+C, Strg+X, Strg+V, Esc | alle wählen, kopieren, ausschneiden, einfügen, Auswahl aufheben |
| Rechtsklick auf leere Fläche | einfügen, alle wählen, Klaviatur |
| Doppelklick auf einen Punkt, oder Entf | Band entfernen |
| Kopfhörer-Knopf im Bandstreifen | Solo für das gewählte Band |
| Pfeile oben, Strg/Cmd+Z, Strg/Cmd+Shift+Z | Undo, Redo |
| Wertefelder | ziehen zum Ändern, Doppelklick zum Eintippen, Strg/Cmd-Klick setzt zurück |

Die Farbe eines Bands zeigt, worauf es wirkt: Grün Stereo, Orange Left, Violett Right, Cyan Mid, Pink Side.
Arbeiten Bänder nur auf einem Teil des Stereobilds, zeichnet das Display je eine Summenkurve pro Kanal.

Q = 1 bedeutet bei Cuts und Shelves „ohne Resonanz" (Butterworth). Bei 6 dB/oct hat Q dort keine Wirkung.
Bell, Notch und Band Pass kennen 6 und 18 dB/oct nicht und nehmen die nächsthöhere Stufe.

### Solo

Solo ersetzt den Ausgang durch den Bereich, in dem das Band arbeitet: bei Bell, Notch, Band Pass ein Bandpass
um die Bandfrequenz, bei Low Shelf und Low Cut alles darunter, bei High Shelf und High Cut alles darüber.
Ein Mid-Band lässt nur das Mittensignal hören, ein Side-Band nur das Seitensignal, Left und Right nur ihren Kanal.
Das Display dunkelt ab, was gerade nicht zu hören ist. Solo wandert mit der Auswahl mit, wird nicht gespeichert
und endet, wenn das Fenster geschlossen wird.

### Auto Gain

Auto Gain misst nicht das Signal. Es rechnet aus der EQ-Kurve aus, wie stark sie ein Signal mit rosa Spektrum
zwischen 30 Hz und 16 kHz lauter oder leiser macht, und gleicht das aus. Dadurch pumpt nichts, und der Wert
steht sofort. Bei Material, das stark von einem rosa Spektrum abweicht, trifft die Schätzung entsprechend
weniger genau. Der angezeigte Wert ist die aktuelle Korrektur.

### Undo

Ein Schritt ist eine Handlung: ein Band anlegen, einmal ziehen, eine Folge von Mausrad-Rasten. Änderungen
durch Automation landen in einem eigenen Schritt. Das Laden eines Projekts oder Presets beginnt eine neue
Historie. Die Historie gehört zur Plugin-Instanz und wird nicht mit dem Projekt gespeichert.

## Wie die Filter gerechnet werden

Jedes Band wird als analoger Prototyp entworfen und dann auf zwei Arten in ein Digitalfilter übersetzt:

- **Matched:** Die Pole sitzen exakt dort, wo sie analog sitzen, die Nullstellen werden so gelöst, dass der
  Frequenzgang der analogen Kurve bis zur halben Abtastrate folgt.
- **Bilinear:** an der Bandfrequenz exakt, darüber zunehmend gestaucht.

Das Plugin baut für jede Einstellung beide, misst sie gegen den Prototyp und nimmt die genauere. Bei üblichen
Einstellungen gewinnt Matched praktisch immer, bei sehr schmalen oder sehr resonanten Bilinear. Wechselt
die Wahl, während ein Band bewegt wird, überblendet die Engine. Low Cut, Notch und All Pass sind immer bilinear:
ihre Nullstellen müssen an exakt einer Stelle sitzen.

Das Display zeigt immer die tatsächlich gerechnete Kurve.

## Was diese Version nicht kann

- Kein Linear-Phase-Modus. Beim Dynamic EQ sind Attack und Release nicht einstellbar, und es
  es gibt keinen Mid/Side-Abgriff getrennt für Quelle und Ziel: ein Mid-Band hört auch am Sidechain nur das Mid-Signal.
- Ein dynamisches Band rechnet seinen Filter laufend neu. Gemessen: 8 dynamische Bänder mit 12 dB/oct brauchen
  rund 1 % eines Prozessorkerns, 24 Stück knapp 5 %.
- Low Cut, Notch und All Pass verhalten sich nahe der halben Abtastrate noch wie klassische Digitalfilter.
- Sehr steile High Cuts und Band Pässe weichen tief im Sperrbereich (unter etwa -20 dB) um ein bis drei dB
  von der analogen Kurve ab.
- Werden sehr viele steile Bänder gleichzeitig bewegt (etwa Gain Scale automatisiert bei 24 Bändern mit
  96 dB/oct), steigt die Rechenlast für die Dauer der Bewegung deutlich. Gemessen im ungünstigsten Fall:
  rund ein Fünftel eines Prozessorkerns, in Ruhe 4 %.
- Flat Tilt weicht bei sehr großen Werten nahe der halben Abtastrate leicht von der Geraden ab: gemessen
  höchstens 1,5 % des eingestellten Gains (bei 6 dB also unter 0,1 dB).

## Lokal bauen

Voraussetzung: CMake ab 3.22 und Visual Studio 2022 (Windows) oder Xcode (macOS).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
```

Ergebnis: `build/HolterdipolterFilter_artefacts/Release/VST3/`.

Mit `-DHDP_BUILD_TOOLS=ON` entsteht zusätzlich `hdp_snapshot`. Es rendert die Oberfläche ohne DAW in ein PNG
(`hdp_snapshot bild.png demo 2`) und fährt drei Selbsttests: `interact` (Mausbedienung), `history` (Undo/Redo)
und `selftest` (Zustand, Layouts, Abtastraten, Solo, Auto Gain).

## Aufbau

```
Source/dsp/FilterDesign.h   Filterentwurf, reines C++ ohne JUCE
Source/dsp/EqEngine.h       Audio-Engine: Kaskaden, Glättung, Überblendung, M/S, Solo, Auto Gain, Dynamik
Source/dsp/SketchFit.h      Kurven in Bänder übersetzen (EQ Sketch, EQ Match)
Source/Presets.*            Presets, Factory-Presets, A/B
Source/EditHistory.*        Undo und Redo
Source/Params.*             Parameter und ihre Textdarstellung
Source/PluginProcessor.*    JUCE-Processor, Zustand, Analyzer-Abgriff
Source/PluginEditor.*       Fenster und Kopfzeile
Source/ui/                  Display, Bedienelemente, Farben
Tests/dsp_test.cpp          DSP-Tests
Tools/snapshot.cpp          Entwicklungswerkzeug
```

## Lizenz von JUCE

JUCE steht unter AGPLv3 oder einer kommerziellen Lizenz. Für den eigenen Gebrauch ist das unkritisch.
Wer das fertige Plugin weitergibt, muss eine der beiden Lizenzen erfüllen: https://juce.com/get-juce
