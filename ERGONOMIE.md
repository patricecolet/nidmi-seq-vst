# Où l'UI ment, se tait, ou se contredit

Relevé du 2026-08-14, avant toute correction. Établi par lecture du code, page par
page. Trois catégories :

- **ment** — l'écran affirme quelque chose de faux ;
- **se tait** — l'action est refusée sans un mot ;
- **se contredit** — deux éléments voisins disent l'inverse l'un de l'autre.

Rien n'est corrigé ici. L'ordre est celui de la gravité, pas celui des pages : le
premier point n'est pas de l'ergonomie, c'est une perte de données.

---

## 1. Perte de données silencieuse au-delà du pas 16

> ✅ **CORRIGÉ le 2026-08-14** — `nidmi-sequencer-core` `6df4aef`. Les onze gardes
> passent par `stepExists()`, qui borne sur `rows[row].numSteps`. Cinq tests de
> régression, vérifiés par falsification : en remettant la borne globale, les cinq
> tombent et rien d'autre ne bouge. 234/234.

**Se tait.** C'est le plus grave, et il ne se voit pas.

`SequencerEngine` borne l'écriture d'un pas sur `pattern_.numSteps` — le compteur
**global hérité**, qui vaut 16 par défaut. Or le N **par row** monte à 64, se règle
depuis PATTERN (Enc2, plage 1..64 → `SetRowSteps`), et c'est lui qui pilote
l'affichage comme la lecture. `setRowSteps` ne touche délibérément pas au champ
hérité :

```cpp
pattern_.rows[row].numSteps = numSteps;
// Note : l'engine V1 (step 2) utilise encore pattern_.numSteps pour l'avance.
```

Conséquence : sur une row passée à N = 32, **toute écriture sur les pas 17 à 32 est
jetée sans un mot**. Onze fonctions portent la même garde
(`SequencerEngine.cpp` : 617, 626, 639, 660, 661, 669, 782, 788, 860, 1113, 1118) :

`setStep`, `setStepSpan`, `toggleStep`, `copyStep`, `clearStep`,
`setStepSubPattern`, `clearStepSubPattern`, `detachStepSubPattern`,
`setStepAccent`, `setStepSwing`.

**Et se contredit** : `setStepCCLock` borne, lui, sur `kMaxSteps`. Donc au pas 20
d'une row à N = 32, un P-lock s'écrit et une note ne s'écrit pas. Même page, même
pas, deux comportements.

L'éditeur, lui, a déjà été corrigé sur ce point — son `timerCallback` porte le
commentaire : *« PAS sur le legacy `pat.numSteps` (global, =16 par défaut) qui
plafonnait la sélection à 16 »*. Le curseur va donc jusqu'à 64 et propose d'éditer
des pas que le moteur refuse.

---

## 2. Le profil « Aucun » avale tout le MIDI entrant

**Se tait.** C'est l'explication complète de « REC ne fait rien ».

`rebuildLearnMap()` (`PluginProcessor.cpp:271`) ne remplit sa table **que** depuis le
profil d'appareil actif. Le profil d'index 0 — « Aucun », toujours présent — est
vide : les 128 entrées restent à `-1`.

Or `processBlock` (`:352-377`) fait `midiMessages.clear()` puis ne réinjecte que ce
qui a survécu à cette table. Avec « Aucun » :

- aucun CC entrant n'est enregistrable ;
- aucun CC entrant n'est **transmis en sortie** — le plugin devient un trou noir ;
- le MIDI learn ne peut rien apprendre.

Le choix de ne pas être un MIDI thru est assumé et documenté sur place (*« Seuls les
CC déclarés passent »*). Ce qui manque, c'est que **rien ne le dit à l'écran**. La
page GLOB affiche paisiblement `Profil : Aucun` sans laisser entendre que cette
ligne coupe l'entrée MIDI du plugin.

---

## 3. REC a quatre conditions, et n'en annonce aucune

**Se tait.** Le bouton arme un drapeau ; tout le reste est invisible.

`recordCCValue` n'écrit que si **les quatre** sont vraies :

| condition | où | ce qu'on voit si elle manque |
|---|---|---|
| le CC entrant est déclaré au profil actif | `rebuildLearnMap` | rien |
| le transport est en **PLAYING** | `recordCCValue` : `if (state_ != PLAYING) return 0;` | rien |
| la row est `kind == CC` et son `ccNumber` correspond | `recordCCValue` | rien |
| la row est déjà entrée en lecture (`lastStepIdx != 0xFF`) | `recordCCValue` | rien |

Le refus à l'arrêt est délibéré et bien motivé — *« un potard bougé à l'arrêt ne doit
pas modifier le pattern en silence »*. Mais le silence a changé de camp : c'est le
refus qui est silencieux.

Troisième condition, conséquence non évidente : **REC écrit dans les LANES, jamais
dans les P-locks.** Sur un slot de P-lock, le bouton est armé, l'écran est identique,
et il ne peut rien se passer par construction.

Retour visuel existant, en tout et pour tout : le libellé passe de `Rec` à `REC●`,
ROLL ajoute `· REC` dans son en-tête, et AUTO dessine un contour ambre sur la
cellule LANE — mais seulement si `laneIsCC`. Sur une row en `Note`, il n'y a
strictement rien à voir.

---

## 4. Page AUTO — le bouton dit sa destination, la cellule dit son état

**Se contredit.** `PluginEditorEncoders.cpp:789` :

```cpp
setAction(0, aIsCC ? "Row→Note" : "Row→CC", true);
```

Le bouton annonce **ce qu'il fera**, la cellule voisine affiche **ce que la row est**.
Quand la row est en CC, on lit `Row→Note` à côté de `74`. Quand elle est en Note, on
lit `Row→CC` à côté de `Lane`. Un bouton posé contre un afficheur se lit comme un
état : les deux étiquettes semblent alors s'inverser l'une l'autre.

Rien n'est faux dans les données — c'est l'appariement des deux libellés qui est
intenable.

---

## 5. Page AUTO — le sélecteur de champ remappe DEUX encodeurs

**Se contredit**, et plus largement que ce que `SUITE.md` avait relevé.

La bande `Valeur | CC# | Interp` était décrite comme multiplexant Enc2. Elle
multiplexe aussi **Enc1** (`PluginEditorEncoders.cpp:610-636`) : sur le champ
`Valeur`, Enc1 est le **Pas** ; sur `CC#` et `Interp`, Enc1 devient le **sélecteur de
slot** et son étiquette passe de `Pas 3` à `Lane`.

Donc un onglet qui ne montre rien redéfinit silencieusement deux molettes sur
quatre — pendant qu'Enc3 n'a toujours aucune branche sur cette page et continue
d'afficher Vélo/Gate d'un pas, notion qui n'a pas de sens sur une lane.

---

## 6. Page AUTO — « Val 0 » ne distingue pas zéro de rien

**Ment.** `PluginEditorEncoders.cpp:614` :

```cpp
const int v = sd.enabled ? static_cast<int>(sd.note & 0x7F) : 0;
valueEncoderLabel_.setText("Val " + juce::String(v), ...);
```

Un pas inactif affiche `Val 0`, indiscernable d'un pas qui porte réellement la
valeur 0. Le modèle d'écran, lui, tient explicitement la distinction trente lignes
plus loin (`PluginEditorScreenModel.cpp:429`), avec le commentaire qui va bien :
*« un pas inactif reste à -1, donc non dessiné — “CC à 0” et “rien à ce pas” doivent
rester distinguables »*.

Résultat : la barre ne se dessine pas, la molette annonce `Val 0`. Le même pas, lu de
deux façons contradictoires sur le même écran.

---

## 7. La vélocité refuse un pas inactif, sans le dire

**Se tait.** `PluginEditorEncoders.cpp:1046` :

```cpp
if (!pat.rows[sr].step(editBar_, ss).enabled)
    return;
```

La molette tourne, son étiquette suit le geste, et rien n'est écrit.

---

## 8. Deux pages entières ne répondent pas au clic

**Se tait.** `PatternScreen::mouseDown` (`HardwareStyleComponents.cpp:235-434`) ne
traite que quatre pages : `PianoRoll`, `Pattern`, `Harmony`, `Auto`.

`SUITE.md` avait relevé GLOB. **SONG est dans le même cas** et n'avait pas été noté.
Sur ces deux pages, le curseur ne bouge qu'à la molette Param, sans que rien ne
distingue une ligne cliquable d'une ligne qui ne l'est pas — les quatre autres pages
ayant appris le contraire à la main.

---

## 9. Page GLOB — quatre lignes par row sur une page projet

**Se contredit.** Déjà cadré dans `SUITE.md` §2, rappelé pour que le relevé soit
complet : `Type R1`, `CC# R1`, `Interp R1` doublonnent AUTO, et `Canal R1` est du
routage par row. Le symptôme est dans les libellés eux-mêmes — une page globale ne
devrait pas avoir à suffixer ses lignes d'un numéro de row.

---

## 10. Un commentaire qui ment au prochain lecteur

`PluginEditorEncoders.cpp:711`, en tête de `configurePushButtons` :

```cpp
// Pour ce lot, SEULE la Vue HARMONIE câble des fonctions de push (cf. onPushButton).
```

Faux : PATTERN, ROLL, AUTO et GLOB en câblent tous, dans la même fonction, juste en
dessous.

---

## Ce qui marche, et qu'il faut garder

Pour ne pas ne relever que les défauts — deux endroits font exactement ce qu'il
faut, et servent de modèle :

- **ROLL sur une row d'automation** n'essaie pas de dessiner des valeurs comme des
  hauteurs : il affiche une page d'aiguillage, *« ROW D'AUTOMATION — les valeurs se
  lisent sur AUTO »* (`ScreenRollView.cpp:411-416`). C'est le seul refus **explicite**
  de toute l'interface. C'est la forme que devraient prendre les points 1 à 3 et 7.
- **Les lignes d'action de GLOB** neutralisent l'encodeur Valeur et affichent `—`
  plutôt que de laisser traîner la valeur de la ligne précédente
  (`PluginEditorEncoders.cpp:111-116`).

---

## Lecture d'ensemble

> **Correction du 2026-08-14, après relecture de `VISION_ERGO_HARMONIE.md`.** La
> première version de cette section proposait de rendre les refus **lisibles**. C'est
> faux, et Patrice l'a corrigé : *« rien ne doit être bloquant, il faut éviter de
> forcer l'utilisateur à chercher comment ça marche »*. Un refus bien expliqué reste
> un mur. La bonne forme n'est pas un message, c'est **un comportement par défaut
> raisonnable** — voir §Remèdes.

Deux causes suffisent à produire les dix points.

**Un modèle de données à deux têtes** — `pattern_.numSteps` hérité contre
`rows[r].numSteps` par row. Le point 1 en vient entièrement, et sa gravité tient à ce
qu'aucune couche ne signale le désaccord : l'UI propose 64, le moteur en accepte 16.

**Une spec matérielle périmée**, d'où découlent les points 3 à 9. Les sources ne
disent pas la même chose :

| source | surface |
|---|---|
| `CLAUDE.md:87`, `CAHIER_DES_CHARGES_V1.md` §10.1 | 4 encodeurs, écran ~320×240 |
| `VISION_ERGO_HARMONIE.md` §3 | **5 encodeurs** : Curseur, Hauteur, Vélo, Gate, Master |
| `nidmi-seq-hardware/README.md` | **5 encodeurs**, écran 4,0″ **480×320**, surface **figée** |

Le dépôt hardware désigne `VISION_ERGO_HARMONIE.md` comme source de vérité
ergonomique. Le VST, lui, est codé contre les quatre encodeurs de l'ancienne spec.
**Tout le multiplexage relevé ici sert à faire tenir cinq attributs dans quatre
molettes** : le sélecteur de champ d'AUTO (§5), les bascules push qui portent des
valeurs, les réglages par row réfugiés sur GLOB (§9), les libellés qui doivent dire à
la fois l'état et l'action (§4). Le cinquième encodeur les supprime tous.

Ce n'est donc pas « le boîtier est une contrainte périmée sur un écran de 1000 px » —
c'était l'erreur de la première version. Le boîtier **est la cible** : le VST existe
pour le prototyper. La contrainte est réelle, elle est simplement mal recopiée.

Au passage, `SUITE.md` §1 justifie un gain de place *« sur un 320×240 »* : chiffre
périmé lui aussi, l'écran cible fait 480×320.

---

## Remèdes — forme générale

**Aucun refus. Un défaut raisonnable à la place.** Pour chaque point qui « se tait »,
la question n'est pas *« comment le dire ? »* mais *« que devrait-il faire ? »* :

| point | aujourd'hui | forme visée |
|---|---|---|
| 1 — pas > 16 | ~~écriture jetée~~ ✅ corrigé | le moteur borne sur le N de la row, comme l'UI |
| 2 — profil « Aucun » | MIDI entrant avalé | l'inconnu passe en identité ; le profil ne fait que **nommer** |
| 3 — REC à l'arrêt | refus | **step-record** (convention Elektron/MPC), REC en rouge |
| 3 — REC sans profil | refus | enregistre le **numéro** de contrôleur brut |
| 3 — REC sur P-lock | inerte | écrit le P-lock du pas courant |
| 7 — vélo sur pas inactif | refus | active le pas et pose la vélocité |
| 8 — GLOB, SONG | clic mort | cliquables comme les quatre autres pages |

Le seul refus qui reste légitime est celui de ROLL sur une row d'automation — parce
que ce n'est pas un refus mais **un aiguillage** : il dit où la chose se règle.

Et pour le point 2, ce n'est pas seulement de l'ergonomie : `INTEGRATION.md` (dépôt
`synth`) stipule que *« le profil est purement cosmétique et ne modifie aucun message
MIDI »*. `rebuildLearnMap()` en a fait un filtre d'entrée. Le code contredit son
propre contrat.
