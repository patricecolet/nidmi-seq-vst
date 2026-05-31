# NiDMI Seq — Cahier des charges V1

Document de synthèse consolidant les décisions prises lors de la phase de design collaborative. Tient lieu de contrat d'implémentation pour la V1. Les sections "V1.5" et "V2" sont des engagements de non-régression : ne pas peindre le design V1 dans un coin qui bloquerait ces évolutions.

---

## 0. Résumé exécutif

**NiDMI Seq** est un séquenceur MIDI hardware (ESP32-S3) prototypé en VST, conçu pour la **composition rapide multi-pistes** avec synthé polyphonique multi-timbral cible (Waldorf M). Il se distingue par trois piliers :

1. **Subdivision libre de la mesure** — chaque row a son propre N ∈ [1, 64], les rows sont polyrythmiques mais **toutes ré-ancrées au premier tick de chaque mesure** (V1).
2. **Harmonie de premier plan** — progressions d'accords type Hapax (degré + qualité + extensions + inversions), pistes en degrés par défaut, reharmonisation automatique selon l'accord courant.
3. **Ergonomie hardware-first** — OLED 256×64, deux encodeurs, clavier piano 16 blanches (pas) + 11 dièses (fonctions) + Shift, grammaire de gestes unifiée (tap / long-press / hold).

Références musicales : Frank Zappa, Squarepusher. Références ergonomiques : Squarp Hapax (harmonie), Synthstrom Deluge (philosophie machine), Elektron Digitakt (grammaire boutons), Torso T-1 (polyrythmie), Polyend Play (composition rapide).

---

## 1. Vision et principes

### 1.1 Cas d'usage principal
Composition rapide de grilles de jazz et de morceaux polyrythmiques complexes. L'utilisateur capture des idées avec peu de gestes, puis finalise l'arrangement soit sur le hardware (chain/song mode), soit dans un DAW via export MIDI.

### 1.2 Les 3 piliers différenciants (non négociables)
- **Subdivision homogène de la mesure, par row** — donne la polyrythmie naturelle.
- **Progressions harmoniques comme objet de premier ordre** — donne la reharmonisation live et la modulation sans effort.
- **Interface physique minimale mais grammaticale** — peu de boutons, beaucoup de fonctions, apprentissage rapide.

### 1.3 Règles d'arbitrage
Si une simplification met en cause l'un des trois piliers, chercher une autre voie. Si une optimisation prématurée complexifie l'ergonomie, la reporter.

---

## 2. Architecture générale

```
┌─────────────────────────────────────┐
│       Plugin VST (prototype UX)     │
│    JUCE / AudioProcessor / APVTS    │
└──────────────┬──────────────────────┘
               │ SequencerCommand (lock-free FIFO)
┌──────────────▼──────────────────────┐
│         nidmi-sequencer-core        │
│  SequencerEngine · ClockDriver      │
│  HarmonyEngine · ScaleBank          │
│  Pattern · Project · Chain VM       │
└──────────────┬──────────────────────┘
               │ SeqEvent (NoteOn/Off/CC)
        ▼ MIDI output (VST) / MIDI UART (ESP32)
```

Le **core** est la source de vérité du modèle. Il doit rester portable (PlatformIO ESP32-S3 ET CMake/JUCE). Le **plugin VST** est le front-end d'entrée/sortie audio-MIDI + UI JUCE ; à terme, une front-end ESP32 (firmware hardware) se substitue au VST en conservant le même core.

### 2.1 Contraintes core
- **Aucune allocation dynamique** sur le chemin audio/MIDI.
- **Pas de flottants dans le tick engine** sauf si trivial (déjà entiers 64 bits µs).
- **Tick rate** ≥ 1 kHz (résolution ≤ 1 ms) pour supporter N=64 @ 300 BPM.
- **Toute nouvelle structure** doit être sérialisable ValueTree (persistance VST) ET représentable dans le blob MIDI Sequencer-Specific.

---

## 3. Modèle temporel

### 3.1 Principe fondateur (inchangé)
Une mesure = fenêtre fixe définie par la signature (tsNum/tsDen) et le BPM. Cette fenêtre est divisée en N pas **homogènes**, ancrés sur le **premier tick de mesure**.

### 3.2 Polyrythmie per-row (V1)
Chaque `PatternRow` possède son propre `numSteps` (1..64). Toutes les rows d'un pattern partagent la même fenêtre-mesure et se **réalignent toutes à chaque début de mesure**. Exemple : row 0 à N=16 et row 1 à N=11 divergent au cours d'une mesure et reconvergent au prochain downbeat.

> V2 : option de **longueur de piste indépendante** (pattern length per row) pour autoriser dérives et polyrythmies ouvertes. À ne pas bloquer dans l'architecture V1.

### 3.3 Algorithme — bar-relative sampling
Le moteur n'incrémente plus un pas courant global. À chaque tick du driver :

```
barElapsed = nowUs - barStartUs
if barElapsed >= barDurationUs:
    barStartUs += barDurationUs
    for each row: rowState[r].lastStepIdx = 0xFF   // force re-trigger au prochain sample
    maybeAdvanceProgressionOnPatternLoop()

for each row r:
    step = (barElapsed * row.numSteps) / barDurationUs
    if step != rowState[r].lastStepIdx:
        rowState[r].lastStepIdx = step
        triggerRowStep(r, step, nowUs)
```

**Bénéfices** :
- Stateless côté row (hormis `lastStepIdx` pour détecter les transitions).
- Pas de dérive cumulative : chaque pas est calculé depuis l'ancre de mesure.
- Changement de N en cours de mesure = naturellement absorbé au prochain tick.
- Réalignement mesure = gratuit.

### 3.4 Tick rate et driver
Le `SequencerClockDriver` appelle `engine.tick(nowUs)` à ≥ 1 kHz. Pour robustesse au blockSize DAW variable : l'engine peut sur-itérer dans un processBlock (scan fin de `blockStart` à `blockEnd` en pas ≤ 1 ms).

### 3.5 Sources temporelles (inchangé, trois modes exclusifs)
- **Host transport** (DAW playhead).
- **MIDI clock** (24 ppqn, Start/Continue/Stop).
- **Interne / manuel** (boutons Play/Stop).

---

## 4. Modèle de pattern

### 4.1 Structures (cibles V1)

```cpp
enum class RowKind : uint8_t { Note, CC };
enum class PitchKind : uint8_t { ScaleDegree, ChordTone, Chromatic };
enum class RowHarmonyMode : uint8_t {
    A,          // degré absolu dans gamme (mélodique)
    B1,         // degré dans gamme, reroute via progression
    B2,         // chord-tone de l'accord courant (R, 3, 5, 7, 9, 11, 13)
    Chromatic   // MIDI brut
};

struct StepCCLock {
    uint8_t ccNumber;   // 0..127 ; 0xFF = slot libre
    uint8_t value;      // 0..127
};

struct StepData {
    uint8_t     note      = 60;    // MIDI note OU degré OU chord-tone selon pitchKind
    uint8_t     velocity  = 100;
    uint8_t     gate      = 80;
    bool        enabled   = false;
    uint8_t     subPatIdx = kNoSubPattern;
    bool        accent    = false;
    bool        swingEnable = false;
    PitchKind   pitchKind = PitchKind::Chromatic;  // V1 new
    StepCCLock  ccLocks[8];                        // V1 new
};

struct PatternRow {
    uint8_t          channel       = 0;       // 0..15
    uint8_t          numSteps      = 16;      // V1 new — 1..64
    RowKind          kind          = RowKind::Note;  // V1 new
    uint8_t          ccNumber      = 74;      // V1 new — destination si RowKind::CC
    RowHarmonyMode   harmonyMode   = RowHarmonyMode::B1;  // V1 new
    bool             muted         = false;
    StepData         steps[kMaxSteps];
};
```

### 4.2 Budget mémoire par pattern (V1)
- StepData : ~28 octets × 16 rows × 64 pas = 28 Ko
- Subpatterns : 16 subs × (header + 16 sous-pas × 28 o) ≈ 7,5 Ko
- Progression ChordSlot × 32 = ~0,3 Ko
- Headers / metadata = ~0,2 Ko
- **Total : ~36 Ko par pattern.**

Banque de 16 patterns → 576 Ko. **Dépasse les 512 Ko SRAM interne ESP32-S3.** Deux options :
- **(a)** Réduire le budget par pas (P-locks dynamiques plutôt que tableau fixe) → repasser sous 512 Ko.
- **(b)** Stocker la banque en PSRAM (disponible sur modules S3 typiques), pattern courant copié en SRAM à chaque bascule.

**Décision V1 : option (b)**. PSRAM obligatoire sur le hardware. Plus simple à coder, plus rapide au dev. Revisite possible si le budget hardware devient critique.

> Action : valider la présence de PSRAM suffisante sur le module ESP32-S3 choisi avant de graver le PCB.

---

## 5. Modèle harmonique

### 5.1 Gammes (inchangé, déjà dans le core)
12 gammes prédéfinies : major, natural_minor, harmonic_minor, melodic_minor, dorian, phrygian, lydian, mixolydian, pentatonic_major, pentatonic_minor, blues, chromatic.

### 5.2 Slot d'accord enrichi (V1 new — voie β)

```cpp
enum class ChordQuality : uint8_t {
    Major, Minor, Dim, Aug,
    Dom7, Maj7, Min7, m7b5, Dim7, MinMaj7,
    Sus2, Sus4,
    // extensible
};

struct ChordSlot {
    uint8_t       degree;        // 1..7 dans la gamme mère
    ChordQuality  quality;
    uint8_t       extensions;    // bitfield : 9, 11, 13, b9, #9, #11, b13
    int8_t        bassOffset;    // -12..+12 demi-tons (slash chords, inversions)
    uint8_t       durationSlots; // durée relative (1 = unité de base)
};

struct ChordProgression {
    ChordSlot slots[32];  // V1 : 32 slots
    uint8_t   len;
    uint8_t   idx;
    // advance / reset / current helpers
};
```

Le legacy `HarmonicProgressionBuf` (offsets demi-tons) est **conservé** pour rétrocompat, mais les nouveaux patterns utilisent `ChordProgression`.

### 5.3 Progression par pattern (V1)
Chaque pattern a sa propre `ChordProgression` (32 slots). Progression vide = pas de reharmo. Granularité V1 : **avance par mesure** par défaut, avec option "avancer à la fin d'un subpattern" (déjà géré par le core).

> V2 : granularité sous-mesure (accord sur demi-mesure) si besoin.

### 5.4 Tonalité maître (V1 new)
```cpp
struct ProjectSettings {
    float   masterBpm;
    uint8_t masterRootPc;      // 0..11 — tonalité projet
    uint8_t masterScaleId;     // gamme projet
    // ... chain, macros, etc.
};
```

Chaque `PatternHarmonySettings` gagne `followMasterTonality` (symétrie avec `followMasterBpm`). Exposé APVTS → **transpose projet automatisable**.

### 5.5 Modes de piste (V1 new — 4 modes)
Sélectionnable par row via `RowHarmonyMode` :
- **A** : degré absolu dans la gamme mère → mélodie diatonique classique.
- **B1** (défaut) : degré dans gamme, reroute via progression → équilibre flexibilité/cohérence.
- **B2** : chord-tone de l'accord courant (R/3/5/7/9/11/13) → basslines, arpèges, accompagnement.
- **Chromatic** : MIDI brut → Zappa, atonal, passages hors-gamme.

### 5.6 Résolution note (V1 new)
Remplace l'actuel `snapMidiNote` par :

```cpp
uint8_t resolveDegreeToMidi(
    uint8_t storedValue,
    PitchKind kind,
    RowHarmonyMode mode,
    const ChordSlot& currentChord,
    uint8_t scaleId,
    uint8_t rootPc
);
```

Cette fonction encapsule les 4 modes. Le `HarmonyEngine` gagne un fichier `ChordResolver.cpp`.

---

## 6. Subpatterns (inchangés, rappel)

Déjà implémentés dans le core. Rappel V1 :
- 16 subpatterns par pattern, chacun 1..16 sous-pas + duration en "main steps".
- **Modèle retrig** : un subpattern déclenché occupe `duration` pas de la row (masque les pas suivants), joue ses sous-pas, puis la row reprend.
- Subpattern peut override harmony (scaleId, rootPc) et timing (swing, accent).
- `advanceProgOnEnd` : la progression peut avancer à la fin d'un sub plutôt qu'à la mesure.

### 6.1 Adaptation polyrythmie (V1 new)
`RunningSubPattern` stocke la **subdivision au moment du trigger** pour rester cohérent si la row change de N pendant l'exécution.

```cpp
struct RunningSubPattern {
    uint8_t subPatIdx;
    uint8_t currentSubStep;
    uint8_t channel;
    uint8_t triggerRowNumSteps;  // V1 new
};
```

### 6.2 Limite V1 — un seul sub actif global
Le core conserve un unique `RunningSubPattern`. Deux rows qui triggerent au même pas → l'une écrase l'autre.

> V2 : `RunningSubPattern[kMaxRows]` pour parallélisme complet.

---

## 7. Automation

### 7.1 Per-step CC (P-locks, V1)
- **8 slots `StepCCLock` par pas** (et par sous-pas).
- Sentinelle `ccNumber == 0xFF` = slot libre.
- Émission : les CC sont envoyés au déclenchement du pas, **avant** la NoteOn.

### 7.2 CC-tracks (V1)
- Toute row configurable `RowKind::Note` ou `RowKind::CC`.
- Row CC : chaque pas émet `CC[ccNumber] = step.note` (la valeur "note" est réinterprétée comme valeur CC 0..127).
- La UI d'édition affiche une barre de niveau par pas pour les rows CC.

### 7.3 Macros (V1)
- **8 slots × 8 destinations** = 64 mappings totaux. 256 octets.

```cpp
struct MacroDestination {
    uint16_t paramId;    // cible (param pattern / param row / CC externe)
    int8_t   depth;      // -100..+100 (% influence)
    uint8_t  polarity;   // 0 = add, 1 = subtract
};

struct Macro {
    MacroDestination dests[8];
    uint8_t          value;  // valeur courante 0..127 (exposée APVTS)
};

struct ProjectSettings {
    // ...
    Macro macros[8];
};
```

Chaque macro exposé dans l'APVTS → automatisable DAW. Update des destinations en cascade à chaque changement de valeur du macro.

Édition : une page OLED = 8 destinations (2×4) = tout le macro sous les yeux.

### 7.4 Host automation (continu)
Tout param exposé dans l'APVTS est gratuit en automation hôte. Params cibles V1 : masterBpm, masterRootPc, masterScaleId, swing, accent, loop, numSteps (par row), mute (par row), progressionIdx, macros × 8.

### 7.5 Reportés
- **LFOs par row** → V1.5.
- **Motion sequencing** → V2.

---

## 8. Projet, banque, chain

### 8.1 Structure projet (V1 new)

```cpp
struct Project {
    ProjectSettings settings;   // master BPM / tonality / macros
    Pattern         patterns[16];
    ChainSlot       chain[64];
    uint8_t         chainLen;
    // metadata (nom, date, etc.)
};
```

### 8.2 Chain VM (V1 new)
Programme de 64 slots max, opcodes couvrant la notation musicale classique/jazz :

```cpp
enum class ChainOp : uint8_t {
    PlayPattern,       // param1 = patIdx, param2 = repeats (1..16)
    RepeatBegin,       // param1 = count
    RepeatEnd,
    Segno,
    DalSegno,
    DalSegnoAlCoda,
    Coda,
    ToCoda,
    DaCapo,
    DaCapoAlCoda,
    Fine,
    End,
};

struct ChainSlot {
    ChainOp op;
    uint8_t param1;
    uint8_t param2;
};
```

### 8.3 Runtime chain
```cpp
struct ChainRuntime {
    uint8_t pc;
    uint8_t segnoPc;
    uint8_t codaPc;
    bool    dcDsTaken;
    struct Frame { uint8_t beginPc, remaining; };
    Frame   repStack[8];      // jusqu'à 8 niveaux imbriqués
    uint8_t repDepth;
};
```
Interpréteur ~20 lignes C++. Empreinte ~16 octets. Mise à jour à chaque fin de pattern (ou fin de chain de repeats interne).

### 8.4 Édition hardware
Page SONG : liste verticale des slots avec glyphes lisibles (▶ play, ⤴ D.C., Ⓒ coda, ■ end). Enc2 = curseur, Enc1 = modifier ligne active, Shift + blanches = insertion rapide d'opcodes.

---

## 9. Interop MIDI

### 9.1 Export (V1, côté VST uniquement)

**Deux modes** :
- **Bake** : MIDI Type 1 avec NoteOn/NoteOff + CC à valeurs absolues (progression résolue au moment de l'export). Meta minimal (signature NiDMI). Partage immédiat avec DAW ou autres musiciens.
- **Full** : Bake + `Sequencer-Specific Meta Event` (0xFF 0x7F) contenant la sérialisation binaire complète du projet. Les DAW ignorent le blob ; un NiDMI futur le relit pour restaurer 100 %.

Le blob **est** le format de sauvegarde de projet universel portable.

### 9.2 Import (V1.5)
- Détection du blob NiDMI → restauration complète.
- Fallback : mapping manuel tracks→rows, quantification à N choisi, import en mode Chromatic (aucune devinette harmonique).

### 9.3 Hardware ESP32 V1
Pas de MIDI file I/O en V1 hardware. Projets sauvegardés en flash (LittleFS ou équivalent). Peut être ajouté V1.5+ via SD/USB MSC.

---

## 10. UI hardware-style

### 10.0 Vocabulaire (révision 2026-05)

Termes fixés, sans collision (« mode » est **réservé** à son sens musical : modes de gamme, `RowHarmonyMode`) :
- **Vue** — ce qu'on édite : PATTERN · PIANO ROLL · HARMONIE · AUTO · GLOBAL · SONG. Même donnée, angles différents ; une seule Vue affichée à la fois. **Les touches sont le miroir de la Vue active.**
- **Page** — fenêtre de **16 pas** (P1–P4) à l'intérieur d'une Row dont le N > 16 ; navigation sur les **touches noires** (Vues PATTERN + AUTO).
- **Pattern** — la séquence · **Row** — la piste (son tuplet N) · **Step** — un pas parmi N.

### 10.1 Écran graphique ~320×240 couleur (révision 2026-05)

> **Révision 2026-05 — supersède l'OLED 256×64 mono.** Pour rendre la composition lisible (polyrythmie multi-row, progression harmonique, automation) tout en gardant le hardware comme source de vérité, l'afficheur passe à un **TFT graphique couleur ~320×240 (SPI)**, dans les capacités d'un ESP32-S3. L'ergonomie reste pilotée par **touches + encodeurs** : l'écran ne fait qu'**afficher** l'état ; il n'est pas tactile. Le layout « 2 lignes × 4 colonnes / 8 params » de la V1 mono devient un cas particulier (page GLOBAL), pas la contrainte d'affichage générale.

L'écran est **le hub d'édition** : il doit porter toutes les conditions d'une édition de séquence facile. Il fonctionne en **Vues** (une seule affichée à la fois, barre de Vues en haut, bascule par le bouton `Vue` / `Shift+Vue`) :
- **PATTERN** — grille multi-row : chaque row affiche ses `numSteps` (N) répartis sur la largeur de la mesure → la polyrythmie est visible (rows à N différents divergent puis réalignent au downbeat). Indique par row : N, canal, mode harmonique, mute, playhead. La row sélectionnée est mise en évidence (c'est elle que les 16 blanches éditent). **Navigation verticale** : l'écran est un viewport qui montre autant de rows que la hauteur le permet (≥ lisible) et **défile** pour garder la row sélectionnée visible (flèches ▲▼ si rows hors champ).
- **PIANO ROLL** — édition mélodique de la row sélectionnée : Y = hauteur (degré / chord-tone / chromatique selon `RowHarmonyMode`), X = les N cases du tuplet. Piano-roll **tuplet-aware** (l'axe X suit le N de la row, pas un temps absolu). Épuré (révision 2026-05) : pas de sélecteur de champ — Note=Enc1, Pas=Enc2, **Vélo=Enc3**, **Gate=Shift+Enc3**, Zoom octaves=Enc4. Chaque **bloc de note** encode **luminosité ∝ vélocité** et **largeur ∝ gate** ; **lane de vélocité** en bas (visualisation + clic). Sur PATTERN la luminosité des cellules ∝ vélocité.
- **HARMONIE (PROG)** — slots de la progression d'accords en bande horizontale (degré + qualité + extensions), slot courant surligné.
- **AUTO** — lane d'automation : valeurs des P-locks CC par pas pour la row/slot CC sélectionné (histogramme).
- **GLOBAL** — params projet (BPM, signature, tonalité maître, sync, macros) ; conserve la grille de params à curseur (héritage du layout 8-params).
- **SONG** — édition du chain / arrangement (V1.5, cf. §12).

**Tuplets imbriqués :** un pas peut héberger un subpattern (son propre N). On entre dans le subpattern et on édite le sous-tuplet avec la même grammaire ; un fil d'Ariane indique le niveau (`Pattern ▸ R3 ▸ P5 ▸ Sub`).

Barre de titre commune : contexte courant + transport (▶/■) + BPM + signature.

### 10.2 Encodeurs (contextuels — révision 2026-05)

**4 encodeurs poussoirs** (révision : +2 vs la V1 à 2 encodeurs ; un encodeur ≈ 1 $, reste bon marché) :
- **Enc1 = valeur** (édite le champ sous le curseur).
- **Enc2 = curseur** (navigue) ; **push Enc2 = change le champ édité** par Enc1.
- **Enc3 = Vélo** (dédié) : vélocité du pas sélectionné, **dans n'importe quelle Vue** (luminosité PATTERN / lane ROLL suivent). **Shift+Enc3 = Gate** (émule le *push* de l'encodeur-poussoir → articulation : durée de la note en % du pas).
- **Enc4** (contextuel à la Vue) : **PATTERN/AUTO** → **sélection de Row** (le viewport défile pour la garder visible) ; **PIANO ROLL** → **zoom = nombre d'octaves visibles** (1 à ~11, toute la plage MIDI).
- **Shift + Enc1 = réglage fin** (step ÷10 pour valeurs continues).
- Le curseur **ne saute pas de Vue** au bord ; changer de Vue = bouton `Vue` (Shift+Vue = sens inverse).

Le **sens** des encodeurs dépend de la Vue active (cf. §10.1) :

| Vue | Enc2 (curseur) | Enc1 (valeur) — champ par défaut |
|---|---|---|
| **PATTERN** | **pas** (curseur) | **N (tuplet) de la row** → re-subdivision live (push Enc2 : N → canal → mode harmo → mute) ; la **Row** est sur Enc4 |
| **PIANO ROLL** | pas (case du tuplet) | champ du pas : **Note / Vélo / Gate** (push Enc2 cycle le champ) |
| **HARMONIE** | slot de progression | **Degré** (Shift+Enc1 = Bass) ; **Enc3 = Qualité** (Shift = Durée) ; **Enc4 = Extensions**. Épuré : pas de sélecteur de champ |
| **AUTO** | pas | valeur du P-lock CC |
| **GLOBAL** | paramètre projet | valeur du paramètre |

> Principe : sur PATTERN, le geste par défaut **est** le tuplet (Enc1 règle N). C'est le cœur de l'ergonomie.

### 10.3 Les touches = miroir de la Vue active (révision 2026-05)

Décision fondatrice : **les 27 touches reflètent toujours la Vue affichée** (plus d'overlay caché — supprimé). Couche différenciée par LED RGB (§10.5). Par Vue :

| Vue | 16 blanches | 11 noires |
|---|---|---|
| **PATTERN** | tap = toggle le **pas** | R- / R+ (Row) ; **Page- / Page+** (adaptatif) |
| **PIANO ROLL** | **clavier diatonique** → pose la hauteur du pas sous le curseur (degrés de la gamme maître) | Oct- / Oct+ = **défilement vertical** (la fenêtre de hauteurs suit le clavier) |
| **HARMONIE** | blanches 1–7 = **degrés I–VII** du slot | qualités (maj/min/7/…) |
| **AUTO** | tap = pose/retire le **P-lock** du slot actif | Slot- / Slot+ ; **Page- / Page+** (adaptatif) |

- **REC** : sur PIANO ROLL, REC ON = **step-record** (la touche pose la note et le curseur avance). REC OFF = pose sur le pas courant sans avancer.
- **Pages de pas** (N > 16) : les blanches montrent une fenêtre de 16 pas ; les noires **Page- / Page+** la déplacent (navigation **adaptative**, bornée à `ceil(N/16)` pages) ; l'écran surligne la fenêtre éditée et affiche `P2/4`.
- **KEYS / ARP** (V1.5) : modes mélodiques dédiés (jeu live, arpège **calé sur le N** → polyrythmique natif). L'audition live des touches (son hors lecture) nécessite un chemin note-live, différé.

Inspirations assumées (familiarité) : Elektron (pas / pages / p-locks), arpégiateurs Roland/Korg (ARP), clavier des grooveboxes (KEYS).

### 10.4 11 touches noires + Shift
Fonctions (révision 2026-05) :
- Row select (R- / R+) / Mute / Solo
- Copy / Paste / Clear
- Bascules de couche/contexte : Sub (entrer/sortir subpattern), Keys, Arp
- Fill / Randomize

Règles posées :
- **Shift + touche = seconde fonction.** Pas de triple-modificateur.
- **Une seule Vue active** à la fois, visible en barre de titre de l'écran TFT.

### 10.5 LEDs RGB par blanche
Requis pour différencier la couche (PATTERN / PIANO ROLL / HARMONIE / AUTO) / step actif / step trigger sub. WS2812 ou SK6812 (1 bus). À prévoir dans la conception PCB.

### 10.6 N-key rollover
Clavier doit détecter jusqu'à **16 touches simultanées** (saisie rapide, accords en jeu live). À prévoir dans la matrice de scan.

### 10.7 Vues d'édition (cf. §10.1)
1. **PATTERN** — pas × row sélectionnée (contexte principal, tuplet-first).
2. **PIANO ROLL** — édition mélodique tuplet-aware de la row.
3. **HARMONIE (PROG)** — slots de la progression harmonique du pattern.
4. **AUTO** — P-locks CC par pas.
5. **GLOBAL** — BPM, signature, tonalité maître, sync, macros, config.
6. **SONG** — édition du chain / arrangement (V1.5).

Plus le **sous-contexte SUB** (édition d'un subpattern, tuplet imbriqué). **Implémenté (rév. 2026-05)** : sur PATTERN, pas sélectionné + **noire « Sub »** crée (si besoin) et **entre** dans le sub ; **fil d'Ariane** `R3 ▸ P5 ▸ SUB rel/abs` en barre de titre ; **noire « Back »** remonte, **noire « Mode »** bascule relatif/absolu. **On reste dans le sub en changeant de Vue** : PATTERN = sous-pas on/off (Enc1 = N du sub) ; **PIANO ROLL = hauteurs des sous-pas** (clavier diatonique, Enc1 = hauteur). **Correspondance avec la note hôte** : mode **relatif** = les sous-pas sont des **intervalles ancrés sur la note du pas** (piano-roll à **ligne d'ancrage**), ils transposent avec la mélodie de la row ; mode **absolu** = hauteurs fixes. Le contenu du sub est **visible niché dans la cellule du pas hôte** (mini-grille). Ajouts moteur : **`SetSubPatternSteps`** (N éditable) et **`SetSubPatternRelative`** + mode relatif (ancre = note hôte). Profondeur V1 = 1 niveau.

---

## 11. Cible hardware

**ESP32-S3** (Xtensa LX7 dual-core @ 240 MHz, 512 Ko SRAM interne, PSRAM 8 Mo typique).

### 11.1 Répartition cores
- **Core 0** : audio/MIDI/engine, déterministe, temps-réel.
- **Core 1** : UI (scan clavier, OLED, encodeurs, LEDs).

### 11.2 Mémoire
- Pattern courant + engine : SRAM interne.
- Banque de patterns : PSRAM, copie vers SRAM à chaque bascule.
- Project state : flash (LittleFS).

### 11.3 I/O
- MIDI UART (IN/OUT) minimum.
- USB-MIDI (via USB natif de l'S3).
- TRS MIDI (type A ou B — à fixer).
- 1 écran TFT graphique couleur **~320×240 (SPI)** — révision 2026-05, supersède l'OLED 256×64 mono (cf. §10.1). SPI haute fréquence pour un refresh fluide du playhead.
- 27 touches (16 blanches + 11 noires), **5 boutons (Play · Stop · Rec · Vue · Export)**, **4 encodeurs poussoirs** (Valeur · Curseur · Vélo · Zoom) + Shift.

---

## 12. Roadmap

> **Identité (révision 2026-05) : séquenceur MIDI-only**, hardware bon marché, ergonomie des meilleures machines (Elektron / MPC / Polyend / OP-Z). Cœur de valeur = **tuplets / polyrythmie + harmonie**. MIDI-only → le coût des évolutions est l'**UI/écran**, pas le CPU/RAM (un événement MIDI = quelques octets) : arbitrer l'arrangement/multi-pattern sur le critère ergonomie, pas puissance. **Ne pas glisser vers un clone MPC générique** : toute couche « arrangement » reste phasée et ne doit pas trahir les 3 piliers.

### V1 (cahier de ce document)
Core refactor + VST UI hardware-style complète (écran TFT à Vues, tuplet-first) + export MIDI. Toutes les fonctionnalités listées ci-dessus.

### V1.5
- **Arpégiateur** : mode des touches, arpège calé sur le N de la row → arp polyrythmique natif (cf. §10.3).
- **Couche arrangement (type MPC, MIDI-only)** : patterns = clips. (a) **matrice de lancement** de patterns sur les touches (style Session, colle au hardware) ; (b) **arrangeur linéaire** de patterns sur une timeline de mesures (s'appuie sur la banque + ChainVM) ; (c) **import de clip MIDI** comme pattern. Câblage de la ChainVM au moteur.
- LFOs par row.
- Macros : destinations étendues ou scènes multiples.
- Hardware : chargement MIDI depuis SD.

### V2
- **Lecture multi-pattern simultanée** (parallélisme type MPC/Ableton). Léger côté silicium grâce au MIDI-only ; le point de décision est l'ergonomie/écran et le respect des piliers.
- Longueur de piste indépendante (polymétrie vraie, dérive).
- Sub-pattern parallèle inter-row.
- Motion sequencing.
- Song mode étendu (multi-projet, set de morceaux live).

---

## 13. Plan d'exécution du refactor core

### 13.1 Étapes ordonnées

| # | Étape | Livrable | Durée |
|---|---|---|---|
| 1 | **Infrastructure tests** | Catch2 via FetchContent ; cible `nidmi_seq_core_tests` ; 1 test sentinelle "pattern N=16 joue 16 notes/mesure" | 0,5 j |
| 2 | **`PatternRow.numSteps` + `RowKind`** | Ajout champs ; commandes `SetRowSteps`, `SetRowKind` ; serialization ValueTree | 1 j |
| 3 | **`engine.tick()` bar-relative** | Refactor fondateur ; `lastStepIdx` per row ; réalignement mesure | 1 j |
| 4 | **P-locks CC dans `StepData`** | 8 slots × 2 octets ; émission au trigger avant NoteOn | 0,5 j |
| 5 | **`SequencerClockDriver` tick haute-résolution** | Appelle `tick(nowUs)` à ≥ 1 kHz ; sur-itération par block | 0,5 j |
| 6 | **Subpatterns × per-row** | `RunningSubPattern.triggerRowNumSteps` ; reproj durée | 0,5 j |
| 7 | **`ChordSlot` + `ChordProgression` + `resolveDegreeToMidi`** | Nouveau modèle harmonique β ; 4 modes de row | 1 j |
| 8 | **`ProjectSettings` + master tonality** | `followMasterTonality` per pattern ; APVTS exposé | 0,5 j |
| 9 | **Macros engine** | Cascade MacroDestination ; exposition APVTS | 0,5 j |
| 10 | **Chain VM + runtime** | Opcodes, interpréteur, serialization | 1 j |
| 11 | **Tests d'intégration + polish** | Golden tests polyrythmie/harmonie/chain | 1 j |

**~8 jours** de dev core focalisé.

### 13.2 Stratégie de tests
- **Catch2** via FetchContent (dépendance dev uniquement, pas de runtime).
- **Golden tests** : chaque test joue un pattern prédéfini sur N mesures simulées, compare le `SeqEventQueue` produit à un buffer de référence versionné dans le repo.
- Tests fondateurs : N=16 régulier · N=11 régulier · N=16+N=11 polyrythmique · changement BPM · subpattern simple · subpattern avec override harmony · progression 4-accords · chain avec RepeatBegin/End · chain avec D.C. al Coda · macro qui cascade vers 3 CC.

### 13.3 Validation manuelle VST après refactor
- Standalone : pattern polyrythmique audible sur Waldorf M ou équivalent virtuel.
- VST dans DAW : automation host du macro, automation masterRootPc en live.
- Round-trip save/load (VST state + MIDI full export + réimport).

---

## Annexe — fichiers impactés (estimatif)

### `nidmi-sequencer-core/`
- `src/nidmi_seq/StepTypes.h` — PatternRow.numSteps, RowKind, StepData.ccLocks, ChordSlot, ChordProgression, ProjectSettings, ChainSlot.
- `src/nidmi_seq/SequencerEngine.h/cpp` — nouveau modèle tick bar-relative, nouvelles API setRowSteps/setRowKind/setChordProgression/setProjectMaster, macros cascade.
- `src/nidmi_seq/SequencerClockDriver.h/cpp` — tick haute résolution.
- `src/nidmi_seq/HarmonyEngine.h/cpp` — resolveDegreeToMidi, ChordResolver.
- `src/nidmi_seq/SequencerCommandApi.h/cpp` — nouvelles commandes.
- `src/nidmi_seq/ChainVM.h/cpp` — **nouveau fichier**, mini-VM song-mode.
- `tests/` — **nouveau dossier**, Catch2 tests.
- `CMakeLists.txt` — FetchContent Catch2, cible `nidmi_seq_core_tests`.

### `nidmi-seq-vst/`
- `Source/PatternValueTree.cpp` — serialisation étendue.
- `Source/PluginProcessor.cpp/h` — APVTS étendu (macros, masterRootPc, etc.), sync vers engine.
- `Source/MidiExporter.cpp/h` — **nouveau fichier**, export MIDI bake + full.
- `Source/CommandFifo.cpp/h` — inchangé (mais à vérifier).
- `Source/PluginEditor.cpp/h` + `HardwareStyleComponents.cpp/h` — UI adaptée aux 5 contextes (après refactor core stable).

### Docs / projets
- `CLAUDE.md` — à mettre à jour avec la nouvelle architecture une fois le refactor appliqué.
- `CAHIER_DES_CHARGES_V1.md` — ce document.

---

*Document vivant : toute évolution de scope V1 doit y être reflétée avant implémentation.*
