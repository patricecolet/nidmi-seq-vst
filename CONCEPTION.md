# NiDMI Seq — Conception

> Document de **gel de conception**, destiné à être publié en PDF avec le projet et
> **consultable par l'utilisateur**. Double exigence : juste vis-à-vis du code, et
> compréhensible sans l'avoir lu.
>
> Trois documents voisins, trois rôles :
> `VISION_ERGO_HARMONIE.md` dit **où on va** · `CAHIER_DES_CHARGES_V1.md` dit **ce qu'on
> a contracté** · celui-ci dit **ce qui est décidé, ce qui existe, et ce qui est
> seulement proposé**.

---

## 1. Vocabulaire

Le vocabulaire de ce projet est technique et rien ne le définissait. Il est employé
partout ailleurs comme s'il allait de soi.

| Terme | Définition |
|---|---|
| **Row** | Une piste. Elle divise la mesure en **N** pas, indépendamment des autres — c'est ce qui rend la polyrythmie naturelle. |
| **N** | Le nombre de pas d'une row dans une mesure. Deux rows à N différents divergent puis se réalignent au premier temps. |
| **Pas** | Une case d'une row. Porte une hauteur, une vélocité, une articulation. |
| **Sous-pas** | Un pas peut contenir un **sous-pattern** : sa propre subdivision, jouée dans la durée du pas hôte. C'est le tuplet imbriqué. |
| **Grain** | Le niveau auquel on travaille : row, pas, ou sous-pas. Le Curseur navigue au grain courant. |
| **Contexte** | Ce qui s'ouvre quand on pousse une molette : les réglages de profondeur de l'objet ou de l'attribut qu'elle porte. |
| **Span** | Le nombre de pas qu'un pas occupe. Un span de 3 fait une note longue et masque les deux pas suivants — leur contenu est conservé, simplement pas joué. |
| **Gate** | La proportion du pas pendant laquelle la note sonne. 100 % = legato, 10 % = staccato. Différent du span : le span dit *combien de pas*, le gate dit *quelle fraction*. |
| **Lane** | Une row entière consacrée à un contrôleur (`kind = CC`) plutôt qu'à des notes. Ses valeurs sont reliées entre les pas par une **interpolation**. |
| **P-lock** | Une valeur de contrôleur accrochée à **un seul pas**, sur n'importe quelle row, jusqu'à huit par pas. Rien à interpoler entre deux : c'est ponctuel. |
| **Interpolation** | Comment une lane relie deux pas : `Pas` (saut), `Linéaire` (rampe), `Douce` (courbe en S). |
| **Ancre relative** | Un sous-pattern en mode relatif joue des **intervalles** par rapport à la note du pas qui le déclenche, au lieu de hauteurs absolues. Transposer le pas transpose le sous-pattern. |
| **Mode harmonique** | Comment la note écrite est transformée par l'harmonie courante. `A` = degré dans la gamme mère, `B1` = degré rerouté par l'accord courant, `B2` = notes de l'accord, `Chromatique` = brut. **Réglé par mesure**, pas par row. |
| **Progression** | La suite d'accords du pattern. Chaque slot dure un nombre de **temps**, indépendamment du nombre de mesures. |
| **Lane de tonalité** | Une suite de marqueurs tonique + gamme, qui permet de moduler dans le temps. |
| **Chaîne** | L'arrangement : une suite d'instructions (jouer un pattern, répéter, D.S., Coda, Fine). |

---

## 2. La grammaire des encodeurs

**Cinq molettes, un attribut chacune, le même dans toutes les vues.**

> **Pousser une molette ouvre le contexte de l'objet courant à ce niveau.** La molette
> poussée garde son rôle, le Curseur continue de naviguer, et tout le reste est prêté
> à la profondeur — nommée à l'écran.

| Molette | Tourner | Pousser |
|---|---|---|
| 1 · **Curseur** | l'objet courant, au grain choisi | le contexte de cet objet |
| 2 · **Valeur** | hauteur, ou valeur du contrôleur | ce qui devient de cette valeur |
| 3 · **Vélo** | vélocité | la répartition des vélocités |
| 4 · **Gate** | articulation | la durée au sens large |
| 5 · **Master** | tempo | les réglages de projet |

Huit règles :

1. Un attribut par molette, le même dans toutes les vues. La vue change ce qu'on
   **voit**, jamais ce que la main fait.
2. **Tourner = la valeur.** Sans exception, sans bascule.
3. **Pousser = ouvrir le contexte** de l'objet courant à ce niveau.
4. La molette poussée **garde son rôle**.
5. **Le Curseur n'est jamais prêté.** Quand c'est lui qu'on pousse, une molette de
   plus se libère — quatre au lieu de trois.
6. Tout le reste porte la profondeur, et l'écran **nomme** le contexte.
7. **Un seul niveau.** Pousser à nouveau sort. Pas de pile, donc pas de « où suis-je ».
8. Une **marque permanente** sur les molettes qui ont une profondeur : on ne pousse
   jamais pour voir.

Et une règle de forme, qui vaut pour tout le reste du document :

> **Rien n'est bloquant.** À un refus, on substitue un comportement par défaut
> raisonnable, jamais un message d'erreur. REC hors lecture fait du step-record ; sans
> profil, il enregistre le numéro de contrôleur brut. Le seul refus légitime est un
> **aiguillage** : ROLL sur une lane dit où les valeurs se règlent.

### Hiérarchie du Curseur, par vue

| Vue | Niveaux |
|---|---|
| PATTERN · ROLL | row → pas → sous-pas |
| AUTO | row → pas |
| HARMONIE | lane → slot |
| SONG | chaîne → slot |
| GLOB | liste plate, pas de grain |

### Les huit boutons

| Bouton | Rôle |
|---|---|
| `ROW` · `HARMONY` · `PROJET` | **où on va** — re-appui = cycle dans la famille |
| `VUE` | **comment on regarde** — cycle ce que la vue courante affiche |
| `SHIFT` | note ↔ fonction des touches noires |
| `PLAY` · `STOP` · `REC` | transport |

`EXPORT` n'a pas de bouton : c'est une action de fin de session, elle vit sur la page
PROJET à côté de *Charger / Enregistrer*. C'est le critère de fréquence du §3 appliqué
à la surface matérielle — huit boutons figés, donc tout ajout est un échange.

---

## 3. Critères de placement

**Sans critère, n'importe quel paramètre peut aller n'importe où**, avec un argument
qui sonne juste à chaque fois. Ces trois tests décident à notre place. Les deux
premiers se vérifient mécaniquement.

### 3.1 Le propriétaire décide du niveau

Un paramètre habite au niveau de **la structure qui le porte dans le moteur**.

| Structure | Niveau |
|---|---|
| `PatternRow` | la row |
| `StepData` | le pas |
| `SubPattern` | le sous-pas |
| `ChordSlot` · `KeySlot` | le slot |
| `SequencerEngine` | le projet |

Ça ne se discute pas, ça se vérifie au `grep`. **Corollaire de mise en œuvre :** toute
molette réelle affiche le champ et la structure qu'elle édite, ce qui rend le placement
auditable d'un coup d'œil.

### 3.2 Le niveau n'est pas la vue

**Le niveau dit où le réglage habite. La vue dit où on le regarde.** Confondre les deux
est la source principale de l'arbitraire.

`ccNumber` et `ccInterp` sont des champs de `PatternRow` : leur adresse est
`Curseur ▸ push` au grain row, **et elle ne change pas selon la page**. Sans quoi la
règle « la vue ne change que ce qu'on voit » serait morte.

### 3.3 La fréquence décide de la couche, et le compte force la main

Dans un niveau donné : ce qu'on tourne en composant reste immédiat, ce qu'on règle une
fois descend en contexte ou part sur une page de configuration.

Le critère mord aussitôt : **`PatternRow` a cinq champs éditables pour quatre places.**
Le canal MIDI — le plus « réglé une fois » des cinq — part sur GLOB. Ce n'est pas un
choix de goût, c'est une place qui manque.

> **Zone molle assumée.** Un champ de `StepData` peut aller dans le contexte du *pas*
> (Curseur) ou dans celui d'un *attribut* (Vélo, Gate). `accent` qualifie la vélocité,
> `span` qualifie la durée — défendable, mais ça reste de l'appréciation. Aucun critère
> dur n'a été trouvé ici.

### 3.4 Ce que les critères ont renversé

- **`Type` (Note/CC) disparaît.** Non pas mal placé : **redondant**. Une destination
  suffit à dire ce qu'est la row — pas de destination, elle joue des notes ; une
  destination, c'est une lane. Un booléen de moins, un terme de moins au glossaire, un
  encodeur libéré.
- **Destination et Interpolation ne vivent pas sur AUTO** mais au grain row, puisque
  `PatternRow` les porte. Conséquence inconfortable et assumée : **il ne reste plus rien
  de réel sous la molette Valeur**.
- **Les lignes par row de GLOB n'étaient pas le défaut** que le relevé d'ergonomie
  dénonçait. Une page de configuration est le bon endroit pour du réglé-une-fois ; c'est
  le libellé (`Type R1`, un numéro de row collé au nom) qui trahissait un accès bricolé.

---

## 4. Ce qui existe aujourd'hui

Relevé dans le moteur le 2026-08-14. **Seuls ces champs sont réels** ; tout le reste
du document relève du §4.

| Champ | Structure | Porté par |
|---|---|---|
| `numSteps` `kind` `ccNumber` `ccInterp` `channel` `muted` | `PatternRow` | Curseur ▸ grain row |
| `harmonyMode[bar]` — **par mesure** | `PatternRow` | Curseur ▸ grain row |
| `note` `velocity` `gate` `enabled` `span` `accent` `swingEnable` | `StepData` | couche de base, Vélo ▸ push, Gate ▸ push |
| `subPatIdx` `ccLocks[8]` | `StepData` | Curseur ▸ grain pas |
| `numSteps` `duration` `relativeToHost` `advanceProgOnEnd` | `SubPattern` | Curseur ▸ grain sous-pas |
| `degree` `quality` (6) `extensions` `bassOffset` `durationBeats` | `ChordSlot` | HARMONIE |
| `rootPc` `scaleId` `durationBeats` | `KeySlot` | HARMONIE, lane tonalité |
| `harmonyEnabled` `followMasterTonality` `followProgression` `advanceProgOnSubPatternEnd` | `PatternHarmonySettings` | Curseur ▸ grain lane |
| `op` `param1` `param2` | `ChainSlot` | SONG |
| `bpm` `timeSignature` `ccRateBudget` `songMode` `activePatternIndex` | `SequencerEngine` | Master ▸ push, SONG ▸ push |

Deux pièges relevés à l'audit, contre-intuitifs :

- **`harmonyMode` est indexé par mesure**, pas par row. Une row peut suivre `A` en
  mesure 1 et `B2` en mesure 2.
- **`ChordQuality` compte six valeurs** : majeur, mineur, diminué, augmenté, sus2, sus4.

---

## 5. Registre des idées

Rien ici n'existe dans le code. Chaque entrée porte son origine et ce qu'elle coûte,
pour qu'on décide de l'ensemble plutôt que d'ajouter au compte-goutte.

### 5.1 Le générateur de séquence *(idée de Patrice, 2026-08-14)*

**Un générateur avec des paramètres de répartition, à chaque niveau.**

La forme retenue : ce n'est **pas une page ni un mode**, ce sont les **profondeurs des
attributs eux-mêmes**. Chaque molette possède déjà le bon attribut ; sa profondeur
devient la manière dont cet attribut se répartit.

| Pousser | La profondeur répartit |
|---|---|
| **Curseur**, au grain courant | quels pas sont actifs — densité, euclidien, rotation |
| **Valeur** | les hauteurs — ambitus, marche, aléa, contrainte de gamme |
| **Vélo** | les vélocités — aléa, courbe, accentuation |
| **Gate** | les durées — legato/staccato, aléa |

Deux conséquences qui plaident pour cette forme :

- **« Pour chaque niveau » vient gratuitement.** Le grain du Curseur choisit déjà le
  niveau : répartir au grain row remplit la row, au grain pas remplit le sous-pattern
  du pas, au grain lane remplit la progression.
- **Aucun geste nouveau à apprendre.** Générer, c'est tourner une profondeur.

**Le point à trancher — génération vive ou semée.**

*Vive* : les paramètres font foi, la séquence est calculée. Cohérent, réversible, mais
**toute retouche à la main est écrasée** au prochain tour de molette.
*Semée* : le générateur écrit une fois dans des données éditables. On garde ses
retouches, mais il faut une commande « Générer » **et une place pour la mettre** — les
huit boutons sont pris, les onze noires aussi. C'est un vrai problème de surface.

Piste : un **appui long** sur la molette dont on vient de régler la répartition — le
geste reste sur la molette concernée, aucune place nouvelle n'est consommée. À évaluer
contre la règle 8 (on ne pousse jamais pour voir).

### 5.2 Dispositions d'affichage *(idée de Patrice, 2026-08-14)*

**Ce qu'une vue affiche est une préférence, pas une décision du concepteur** — l'idée
vient de Traktor et Blender, où l'on enregistre des dispositions d'interface.

Le mécanisme **existe déjà** : `padMode` fait encoder aux cases de PATTERN les *pas*,
les *accents* ou le *swing*. On y ajoute **Contrôleur** : chaque row montre alors le
contour de ce qu'elle module — la lane son profil, une row de notes son P-lock. PATTERN
devient une table de mixage.

Deux notes qui vont ensemble :

- **Garantie.** Une préférence d'affichage ne peut rien corrompre, et c'est la règle 1
  qui l'assure : *la vue ne change que ce qu'on voit, jamais ce que la main fait*. C'est
  ce découplage qui rend des dispositions sauvegardables inoffensives.
- **Prudence.** « On le rendra configurable » est le moyen le plus élégant d'éviter de
  choisir un défaut. Le mode *Pas* doit rester bon pour qui ne touchera jamais au
  réglage.

**Accès** : le bouton `VUE` (§2), obtenu en descendant `EXPORT` sur la page PROJET.
Re-appui = cycle ; `⇧ + VUE` = sens inverse. Ce que `VUE` cycle sur ROLL, AUTO et
HARMONIE **reste à définir** — seul le cas de PATTERN s'appuie sur un mécanisme
existant.

**Étape suivante, non décidée** : enregistrer et rappeler des dispositions nommées,
comme les *workspaces* de Blender. À évaluer contre le coût d'interface sur un
480 × 320.

### 5.3 Le mixage MIDI *(idée de Patrice, 2026-08-14)*

Point de départ : **un bouton `MUTE` dédié ne sait faire qu'une chose, sur une seule row
à la fois** — alors que couper une piste est par nature une opération de comparaison,
on la coupe *pour entendre les autres*. Sa place est là où on les voit toutes.

Ce que « mixage » veut dire sur un séquenceur **MIDI-only**, sans rien inventer :

| | |
|---|---|
| **Mute** | réel — `PatternRow.muted` |
| **Solo** | n'existe nulle part |
| **Volume** | pas un champ : **CC 7**, par canal |
| **Panoramique** | pas un champ : **CC 10**, par canal |

Volume et panoramique retombent donc sur la même réponse que le glide (§5.4) : des **CC
connus, nommés par le profil d'appareil**. Aucun paramètre nouveau, aucun moteur à
toucher. Une vue de mixage est surtout **une lecture de ce qui existe déjà**, plus le
mute — ce qui la rend étonnamment bon marché.

**Ce qu'elle répare.** `muted` n'a aujourd'hui **aucune adresse** dans la grammaire : la
répartition l'avait expédié d'une phrase (« c'est un bouton dédié ») alors qu'il n'y a
pas de bouton dédié dans les huit. Et le critère du compte remord : **`PatternRow` a six
champs éditables — N, mode harmonique, destination, interpolation, canal, mute — pour
quatre places.** Le canal est déjà exilé sur GLOB ; le mixage est le logement naturel du
sixième.

**Emplacement visé** : un mode de plus dans le cycle de `VUE` sur PATTERN —
`Pas · Accent · Swing · Contrôleur · Mixage`. Chaque row y montre son niveau, et les
touches blanches coupent les rows : le clavier étant déjà le miroir de la vue, aucun
geste nouveau.

**Pourquoi ce n'est pas implémenté.** Ajouter un cinquième mode à `VUE` avant d'avoir
arrêté l'ensemble des modes, c'est précisément l'ajout au compte-goutte qu'on s'interdit.
Les modes de `VUE` se trancheront d'un bloc — d'autant qu'on ignore encore ce que ce
bouton cycle sur ROLL, AUTO et HARMONIE (§6).

**Arithmétique des boutons, pour mémoire.** Sept sont indiscutables : les trois familles,
`SHIFT`, `PLAY`, `STOP`, `REC`. Il en reste **un**, que se disputent `EXPORT`, `VUE` et
`MUTE`. Le mixage règle le cas de `MUTE`, la page PROJET celui d'`EXPORT` : le huitième
revient à `VUE`.

### 5.4 Autres propositions

| Proposition | Emplacement visé | Origine · état |
|---|---|---|
| **Probabilité d'émission** | Vélo, sur une lane CC | 2026-08-14 — retenue. Comble une molette libre : une valeur de contrôleur n'a pas de vélocité. |
| **Glide** | Interpolation, sur une row de notes | 2026-08-14. Remplit un emplacement mort : `ccInterp` ne s'applique qu'aux lanes, l'encodeur affiche `—` sur une row de notes. **Forme retenue** : pas un paramètre nouveau, mais **CC 5 / CC 65 nommés par le profil d'appareil**. Le portamento est le travail du synthé ; générer le glissando nous-mêmes imposerait du pitch bend, qui est par canal et entre en collision avec le modèle de notes. |
| **Ancrage · Densité · Disposition** | Valeur ▸ push, row Note | Le voicing, 4ᵉ pilier — `VISION §5.2b`. Exige plusieurs `NoteOn` simultanés : **refonte du moteur**, pas un module. |
| **Tension du lissage** | Valeur ▸ push, lane CC | `CCInterp` est un enum sans paramètre. Coût faible. |
| **Aléa · Courbe de vélocité** | Vélo ▸ push | Modulateurs — `VISION §6`. Recouvre en partie le §5.1. |
| **Ratchet / répétitions** | Gate ▸ push | N'existe nulle part. Touche le déclenchement, donc le moteur. |
| **Emprunt vs modulation** | Curseur ▸ push, slot d'accord | `VISION §5.2c` — explicitement non tranché. |
| **Swing global** | Master ▸ push | Le swing réel vit au niveau du sous-pattern. À unifier ou à renommer. |
| **Boucle de la chaîne** | Curseur ▸ push, SONG | `ChainSlot` n'a que `op`, `param1`, `param2`. |

---

## 6. Ce qui reste à trancher

1. **Génération vive ou semée**, et où loger « Générer » (§5.1). Mis de côté : on
   l'implémentera quand il y aura la place.
2. **Master se prête-t-il ?** `VISION §3` dit « BPM, toujours accessible ». Si Master
   est prêté pendant un contexte, le BPM ne l'est plus.
3. **Push maintenu ou verrouillé** pour entrer dans un contexte.
4. **La molette Vélo sur un accord.** J'y ai mis la qualité, au motif que vélocité et
   qualité sont toutes deux « le caractère » de l'objet. C'est le maillon le plus faible
   de la convention. Si une seule molette doit rester contextuelle, ce sera celle-là.
5. **Gate sur une lane CC** reste libre, sans proposition.
6. **Ce que `VUE` cycle** sur ROLL, AUTO et HARMONIE. Seul PATTERN est adossé à un
   mécanisme existant. À trancher **d'un bloc**, mixage compris (§5.3), plutôt qu'un
   mode à la fois.
7. **La polyrythmie n'est plus le principe d'organisation de PATTERN.** Elle reste un
   outil local — un charley en 3 contre 4 n'a rien d'exotique — mais la vue s'organise
   désormais autour de *l'état de chaque row*. Conséquence : `CAHIER_DES_CHARGES_V1.md`
   §12 pose l'identité *« cœur de valeur = tuplets / polyrythmie + harmonie »*. Cette
   phrase doit être vraie ou révisée avant le gel.

---

## 7. Ce que ce document révise

- `CAHIER_DES_CHARGES_V1.md` §10.2 décrit des **encodeurs contextuels** dont « le sens
  dépend de la Vue active ». La grammaire du §2 pose l'inverse. Le cahier est
  **supersédé** sur ce point.
- `VISION_ERGO_HARMONIE.md` §2.3 place le changement de **grain** sur le push du
  Curseur. Le push servant ici à ouvrir un contexte, le grain part sur ⇧+noires, avec
  le reste de la navigation de structure.
- **Le bouton `VUE` revient, avec un autre métier.** Le cahier d'origine
  (§11.3) listait *Play · Stop · Rec · Vue · Export* ; `VUE` y servait à changer de
  page. Ce rôle appartient désormais aux trois boutons de famille. `VUE` reprend le
  huitième bouton — libéré par `EXPORT`, descendu sur la page PROJET — pour dire
  **comment on regarde** et non plus où on va.
- La surface matérielle fait foi côté `nidmi-seq-hardware` : **5 encodeurs EC11,
  8 boutons PB86, écran ILI9488 4,0″ 480×320**, BOM gelée.
