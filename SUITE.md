# Où on en est, et ce qui reste

Point d'étape écrit en fin de session du 2026-08-14. Objectif : reprendre sans
rien reconstituer.

---

## État des dépôts

| dépôt | branche | commit | état |
|---|---|---|---|
| `nidmi-seq-vst` | `main` | `6211ba6` | poussé — 3 formats compilent sans warning |
| `nidmi-sequencer-core` | `main` | `e78d2ca` | poussé — **229/229** tests |
| `synth` | `main` | `10b819b` | poussé — public, licence MIT |

`feat/device-profile` a été fusionnée en **fast-forward** dans `main` (36 commits,
`main` n'avait pas bougé). La branche existe encore et pointe au même endroit ;
elle peut être supprimée.

**Branche à trancher** : `model-hybride` (`d6de1b0`, 12 juin) est locale, absente
du remote, avec 1 commit que `main` n'a pas et ~40 commits de retard. À rebaser ou
à supprimer — laissée intacte faute d'instruction.

---

## La décision d'architecture de cette session

**ROLL = hauteurs. AUTO = contrôleurs.** Un seul onglet pour automatiser.

Les deux pages sont la vue de détail de la row sélectionnée ; dessiner les lanes CC
dans ROLL faisait donc doublon avec AUTO. Une première version le faisait, elle a
été retirée : une row d'automation ouverte dans ROLL y renvoie explicitement.

AUTO porte deux mécanismes voisins mais distincts, côte à côte dans sa bande :

- **LANE** (`autoSlot == kAutoLaneSlot`, soit `-1`) — la row entière comme
  automation (`RowKind::CC`), avec son propre N et une interpolation entre les pas ;
- **P-LOCK** (`autoSlot` 0..7) — une valeur ponctuelle accrochée à un pas, sur
  n'importe quelle row. Rien à interpoler entre deux.

---

## Ce qui est vérifié, et ce qui ne l'est pas

**Vérifié par des tests** — `recordCCValue` (10 tests, dont un aller-retour complet
falsifié en retirant l'activation du pas), l'ancre harmonique des subs relatifs
(4 tests, falsifiés), l'interpolation et le budget de débit.

**Vérifié à l'écran, mesure de pixels à l'appui** — la coloration harmonique des
lanes, le clavier vertical, la courbe d'interpolation d'AUTO, la cellule LANE, le
libellé d'ancre du sub.

**Écrit, compilé, PAS vérifié de bout en bout** :

- **L'enregistrement temps réel** (`processBlock` → `recordCCValue`). Le moteur est
  couvert, le câblage compile, mais aucune entrée MIDI n'a été branchée sur le
  Standalone. **À faire en premier à la reprise** : armer REC, lancer Play, tourner
  un potard sur le CC de la lane — les barres doivent se remplir au pas courant.

---

## Ce qui reste, par ordre

### 1. Le sélecteur de champ d'AUTO — à supprimer

La bande `Valeur | CC# | Interp` est un **sélecteur modal** : elle ne montre rien,
elle décide seulement de ce que l'encodeur *Valeur* édite. Deux entrées existaient
avant cette session, la troisième (`Interp`) a été ajoutée en suivant le motif sans
le questionner.

Le motif ne tient pas : on multiplexe un encodeur **pendant qu'Enc3 ne sert à rien
sur cette page** (aucune branche AUTO — il reste sur Vélo/Gate du pas, qui n'a pas
de sens sur une lane). Et `Interp` est une énumération de trois valeurs : elle veut
un bouton, pas un onglet plus une molette.

Cible :

| Enc1 | Enc2 | Enc3 | Enc4 |
|---|---|---|---|
| Pas | Valeur | **CC#** | Row |

`Interp` sur le push d'Enc3, juste sous la molette qui porte le CC#. La bande
disparaît et libère sa hauteur au profit de la lane — 20 px qui comptent sur un
320×240. Le cas P-lock marche tel quel, Enc3 éditant alors le CC# du slot actif.

### 2. Doublons de la page GLOB

Trois lignes de GLOB sont des réglages **par row** qui existent désormais aussi
dans AUTO. Elles y avaient été mises comme accès de secours (`877c59b`, `bc693c6`),
pas par choix d'ergonomie :

| GLOB | équivalent AUTO |
|---|---|
| `Type R1` | push Param sur la LANE |
| `CC# R1` | champ `CC#` |
| `Interp R1` | champ `Interp` |

Critère : **GLOB porte le projet, AUTO porte la row.** Le symptôme est dans les
libellés eux-mêmes — une page globale ne devrait pas suffixer ses lignes d'un
numéro de row. Restent légitimes sur GLOB : `Débit CC`, `Profil`, les deux resets,
`Pattern`, `Mesures`, `Mode`.

`Canal R1` a le même défaut mais c'est du routage, pas du contrôleur : sa place
serait sur PATTERN, où on voit les rows.

### 3. GLOB n'est pas cliquable

Seule page dont les lignes ne répondent pas au clic — `mouseDown` n'a pas de branche
`Page::Global`. Le curseur ne bouge qu'à la molette Param. Toutes les autres pages
répondent au clic.

### 4. Chantiers de fond, déjà cadrés

- **Lecteur MIDI parallèle** (`RowKind::MidiClip`) et **import du blob NiDMI** : le
  côté export existe (`MidiExporter`, mode Full), la lecture non. C'est
  l'architecture « moteur de tuplets pour l'édition + lecteur SMF en parallèle,
  raccordés par des méta-événements ».
- **Drag & drop de fichiers MIDI** — explicitement reporté par Patrice.
- **Éditeur de panneau** — le format de profil est prêt (image, positions, learn),
  documenté dans `Profiles/FORMAT.md`.

---

## Notes pratiques pour la reprise

**Toujours relancer l'app après un build.** Une analyse a déjà été faite sur un
binaire périmé, avec des conclusions fausses à la clé.

**Piloter l'UI** : poster un vrai `CGEvent` (`ctypes` + ApplicationServices),
**précédé d'un déplacement de souris** — sans ce déplacement le toolkit ignore le
clic. `osascript … "click at {x,y}"` ne clique PAS aux coordonnées : il « presse »
l'élément d'accessibilité sous le point, d'où des changements d'onglet erratiques.

Positions des 4 push, en coordonnées d'une capture retina 2× :

| encodeur | position image |
|---|---|
| Enc1 (haut-droite) | 1862, 626 |
| Enc2 (haut-gauche) | 135, 626 |
| Enc3 (bas-gauche) | 135, 1033 |
| Enc4 (bas-droite) | 1862, 1033 |

**`cp` + rebuild ne suffit pas** après une falsification de test : CMake ne
recompile pas, il faut `touch` le fichier. Deux faux « le test échoue encore » ont
été causés par ça.

---

## Deux pièges consignés dans `CLAUDE.md`

Rappelés ici parce qu'ils ont coûté cher :

- **Dessin et test de clic doivent partager la MÊME géométrie.** Touché deux fois
  cette session — le sub-roll recalculait sa gouttière pour le clic, et HARMONIE
  testait sur `bodyArea_` en dessinant sur `gridArea()`. Aucun des deux ne montrait
  quoi que ce soit d'anormal à l'écran.
- **Ne pas encoder une information « vers le bas » depuis un fond quasi noir.** La
  gamme était dessinée avec 3,7 de contraste de luminance contre 24 pour l'accord :
  dessinée, donc, mais invisible.
