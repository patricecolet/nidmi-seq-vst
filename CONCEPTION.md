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

## 1. Glossaire

Le vocabulaire de la machine est technique, mêlé d'anglais, et rien ne le définissait.
Ce glossaire sert **deux publics** : l'utilisateur qui lit le manuel, et nous qui
écrivons le code — un seul mot par notion, pour ne plus en inventer en chemin.

La colonne *retenu* fixe le mot employé partout : à l'écran, dans le manuel, dans les
commits. Quand l'anglais reste, c'est qu'aucun équivalent français n'est en usage chez
les musiciens.

### 1.1 La séquence

| Retenu | Origine | Ce que c'est |
|---|---|---|
| **pattern** | anglais, conservé | Une séquence complète : ses rows, son harmonie, ses réglages. On en garde plusieurs dans une **banque**. |
| **row** | anglais, conservé | Une piste. « Rangée » traîne encore à un endroit de l'écran — **à uniformiser**. |
| **pas** | fr. (angl. *step*) | Une case d'une row. Jamais « step », sauf dans `step-record`. |
| **N** | — | Le nombre de pas d'une row dans une mesure. Deux rows à N différents divergent puis se réalignent au premier temps. |
| **mesure** | fr. (angl. *bar*) | Une row porte son contenu par mesure, jusqu'à huit. |
| **page** | fr. | Fenêtre de 16 pas, quand une row en compte plus. |
| **sous-pattern** | fr. + angl. | Le contenu qu'un pas peut abriter : sa propre subdivision, jouée dans la durée du pas hôte. C'est le tuplet imbriqué. Deux réglages seulement : son **N** et son **ancre**. Son étalement n'en est pas un — c'est **la durée du pas hôte** qui le commande. `SubPattern.duration` est un vestige de compatibilité, utilisé seulement si le pas n'a pas de durée propre. |
| **sous-pas** | fr. | Un pas d'un sous-pattern. |
| **tuplet** | angl., conservé | Une division qui ne tombe pas sur la grille binaire — triolet, quintolet. Le terme français « n-olet » n'a pas d'usage. |
| **polyrythmie** | fr. | Plusieurs rows à N différents jouant ensemble. Un outil, pas le principe d'organisation. |

### 1.2 Ce que porte un pas

| Retenu | Origine | Ce que c'est |
|---|---|---|
| **hauteur** | fr. (angl. *pitch*) | La note. « Note » désigne l'événement entier, « hauteur » sa seule fréquence. |
| **vélocité** | fr. | La force de frappe, 0 à 127. Abrégée **vélo** à l'écran, faute de place. |
| **durée** | fr. | Le temps pendant lequel la note est tenue, **en pas et fractions de pas**. Une durée qui atteint le pas suivant fait se recouvrir les deux notes, ce dont le synthé tire un legato. Remplace *span* et *gate* à l'écran. |
| ~~gate~~ · ~~span~~ | angl. | **Termes internes**, plus exposés à l'utilisateur. Le moteur en dérive la paire depuis la durée : `span` = le nombre de pas couverts — qui masque les pas suivants et sur lequel un sous-pattern s'étale — `gate` = la fraction restante. La conversion existe déjà (`quartersToSpan`, `quartersToGate`). |
| **accent** | identique | Le pas est renforcé. |
| **swing** | angl., conservé | Le pas est décalé pour créer le balancement ternaire. |
| ~~ratchet~~ | angl. | **Abandonné comme fonction séparée.** Un ratchet — un pas répété 2, 3 ou 4 fois dans sa durée — est le **cas dégénéré du sous-pattern** : celui dont tous les sous-pas répètent le pas hôte. On l'obtient en entrant dans le pas et en réglant son N. Le sous-pattern fait tout ce que fait un ratchet, et davantage : chaque répétition peut avoir sa hauteur, sa vélocité, sa durée. |
| **micro-décalage** | fr. | L'écart entre l'instant joué et le pas où la note s'est rangée. C'est lui qui donne son **grain** à une interprétation enregistrée. |

> **« Grain »** ne désigne plus qu'une chose : le relief d'une interprétation. Il a
> servi un temps à nommer un niveau de navigation — cet emploi est abandonné.

### 1.3 Les contrôleurs

| Retenu | Origine | Ce que c'est |
|---|---|---|
| **CC** | angl. *control change* | Un message MIDI qui règle un paramètre du synthé. Toujours « CC », jamais « contrôleur continu ». |
| **automation** | fr. | Le fait, pour une row, de piloter un CC au lieu de jouer des notes. **Pas un objet : un état.** Donner une destination à une row la met en automation. |
| ~~lane~~ | angl. | **Abandonné.** Une lane n'était pas une sorte de row, c'était une row avec une destination — le mot nommait un objet qui n'existe pas. « Voie », sa traduction exacte, est l'homophone de « voix ». |
| **P-lock** | jargon Elektron, **à trancher** | Une valeur de CC accrochée à **un seul pas**, sur n'importe quelle row, jusqu'à huit par pas. |
| **destination** | fr. | Le numéro de CC visé. Une row sans destination joue des notes ; avec, c'est une lane. |
| **interpolation** | fr. | Comment une lane relie deux pas : *Pas*, *Linéaire*, *Douce*. |
| **débit CC** | fr. | Le plafond de messages par seconde, tous CC confondus. Le MIDI DIN sature vers 1000. |
| **profil d'appareil** | fr. | Une table qui **nomme** les CC d'un synthé — « VCF Cutoff » au lieu de « CC 74 ». Purement cosmétique. |
| **assignation** | fr. (angl. *learn*) | Associer un potard de ton contrôleur à un paramètre du synthé. Concrètement un **remappage** : le CC entrant est réémis sous le numéro du paramètre. Sans assignation, un CC passe sous son propre numéro. |

### 1.4 L'harmonie

| Retenu | Origine | Ce que c'est |
|---|---|---|
| **tonalité** | fr. | Tonique + gamme. Peut changer dans le temps via la **lane de tonalité**. |
| **degré** | fr. | Le rang d'un accord dans la gamme, en chiffres romains. |
| **qualité** | fr. | Majeur, mineur, diminué, augmenté, sus2, sus4. |
| **extensions** | fr. | Les notes ajoutées à l'accord — septième, neuvième. |
| **basse** | fr. | Décalage de la note grave : renversements, accords slash. |
| **progression** | fr. | La suite d'accords du pattern. Chaque slot dure un nombre de **temps**. |
| **mode harmonique** | fr. | Comment la note écrite est transformée. *Délié*, *A*, *B1*, *B2*, *Chromatique*. Réglé **par mesure**. |
| **voicing** | angl., conservé | Comment un accord se dispose. Aucun équivalent français en usage. *(proposé, non implémenté)* |

### 1.5 L'arrangement

| Retenu | Origine | Ce que c'est |
|---|---|---|
| **chaîne** | fr. (angl. *chain*) | La suite d'instructions qui enchaîne les patterns. |
| ~~slot~~ | angl. | **Abandonné**, comme *lane* et *P-lock* : le mot recouvrait deux natures et n'était nécessaire pour aucune. Un élément d'une suite porte le nom de la chose — **accord**, **instruction**. Une des huit automations d'une row se nomme par sa **destination** (« l'automation du CC 74 ») ; un numéro suffit tant qu'elle est vide. |
| **mode Pattern / Song** | angl., conservé | Le pattern boucle, ou la chaîne avance. |
| **instruction** | fr. | Une étape de la chaîne : jouer un pattern, répéter, renvoyer. |
| **Segno · D.S. · Coda · D.C. · Fine** | italien, conservé | La notation de renvoi des partitions. C'est la langue de l'arrangement dans NiDMI, et elle se lit déjà par les musiciens. |

### 1.6 La machine

| Retenu | Origine | Ce que c'est |
|---|---|---|
| **molette** | fr. (angl. *encoder*) | Un des six encodeurs. « Encodeur » pour la pièce, « molette » pour le geste. |
| **pousser** · **appui** | fr. | Le geste sur une molette : *pousser la molette Pas*, *l'appui sur Hauteur ouvre son contexte*. La pièce est un **encodeur à poussoir**. |
| **contexte** | fr. | Ce qui s'ouvre au push : les réglages de profondeur de ce que la molette porte. |
| **vue** | fr. | Ce qu'on regarde : PATTERN, ROLL, AUTO, HARMONIE, GLOB, SONG. |
| **famille** | fr. | Le groupe de vues d'un bouton : ROW, HARMONY, PROJET. |
| **tête de lecture** | fr. (angl. *playhead*) | Le pas en cours de lecture. |
| **saisie pas à pas** | fr. (angl. *step-record*) | REC à l'arrêt : on **saisit** des notes une à une, la grille donne le temps. À distinguer de l'**enregistrement temps réel** (REC en lecture), qui capte les notes *et* leur timing. « Record » était trompeur : on n'enregistre rien, on écrit. À l'écran : `● pas à pas` et `● temps réel`. |
| **quantisation** | fr. | Ramener une note jouée sur la grille. **Optionnelle** : désactivée, le micro-décalage est conservé. |

### 1.7 Incohérences relevées, à corriger

- **`Row` et `Rangées`** coexistent dans les libellés d'écran.
- **`Interp`, `Lineaire`, `Duree`, `Repet`** — abréviations et accents manquants, hérités de la contrainte d'écran. À revoir avec le manuel.
- **Plus aucun terme en attente.** Les sept sont réglés : quatre traduits — `span` → durée, `learn` → assignation, `push` → pousser/appui, `step-record` → saisie pas à pas — et trois **supprimés** parce qu'ils nommaient des objets qui n'existaient pas : `lane`, `P-lock`, `slot`. Tant qu'ils ne sont pas fixés, chaque session les
  renomme à sa façon. 

---

## 2. La grammaire des encodeurs

**Six molettes, un attribut chacune, le même dans toutes les vues.**

> **Pousser une molette ouvre le contexte de ce qu'elle porte.** La molette poussée
> garde son rôle, les deux molettes de navigation ne sont jamais prêtées, et les autres
> portent la profondeur — nommée à l'écran.

**Le contexte de la row**, dans l'ordre des molettes :

| Pas | Valeur | Vélo | Durée | Master |
|---|---|---|---|---|
| **N** | **Mode harmo.** | **Interpolation** | **Destination** | *BPM, jamais prêté* |

Les autres contextes ne disposent donc que de **deux places** — `Row`, `Pas` et
`Master` restant à leur poste. C'est une contrainte, et elle est saine : elle force à
choisir ce qui compte vraiment sous chaque molette.

| Molette | Tourner | Pousser |
|---|---|---|
| 1 · **Row** | la piste | les réglages de la row |
| 2 · **Pas** | le pas dans la row | **entre dans le sous-pattern du pas** |
| 3 · **Valeur** | hauteur, ou valeur du contrôleur | ce que devient cette valeur |
| 4 · **Vélo** | vélocité | la répartition des vélocités |
| 5 · **Gate** | articulation | la durée au sens large |
| 6 · **Master** | tempo | les réglages de projet |

**Deux molettes de navigation plutôt qu'un raccourci.** Le conteneur et l'élément — Row
et Pas, Lane et Slot sur HARMONIE, Chaîne et Slot sur SONG. Elles restent disponibles
même quand un contexte est ouvert : on règle la profondeur d'un pas, on passe au
suivant, sans jamais ressortir.

**Le push de la molette Pas fait entrer dans le pas**, c'est-à-dire dans son
sous-pattern — et le push suivant en ressort. À l'intérieur, la molette Pas navigue les
sous-pas et les places libérées portent le N du sub et son ancre. C'est ce qui remplace
le raccourci clavier qui servait auparavant à changer de niveau.

> **Ce que ça coûte.** La BOM du dépôt hardware était gelée à **5 EC11** et
> `VISION §2.4` posait que *« toute richesse future passe par le PUSH, JAMAIS par un
> nouvel encodeur »*. Le sixième encodeur rouvre la BOM et révise cette règle.
> Côté électronique il n'y a pas d'obstacle — le PCNT offre 4 unités par puce sur deux
> ESP32, soit 8 places pour 6. **C'est la mécanique de façade qui est à recaler** : pas
> de 33 mm entre encodeurs, dans une largeur de 320.

Onze règles :

1. Un attribut par molette, le même dans toutes les vues. La vue change ce qu'on
   **voit**, jamais ce que la main fait.
2. **Tourner = la valeur.** Sans exception, sans bascule.
3. **Pousser = ouvrir le contexte** de l'objet courant à ce niveau.
4. La molette poussée **garde son rôle**.
5. **`Row` et `Master` ne sont jamais prêtées.** `Row` parce qu'on doit pouvoir se
   déplacer sans quitter un contexte ; `Master` parce que le BPM reste sous la main en
   permanence (`VISION §3`). `Pas` ne l'est pas non plus — sauf dans le
   contexte de la row, où elle porte le **N** : le nombre de pas est l'axe des pas, il
   doit tomber sous l'encodeur qui les navigue. La navigation n'est pas perdue pour
   autant, `Row` étant la molette poussée, qui garde son rôle.
6. Tout le reste porte la profondeur, et l'écran **nomme** le contexte.
7. **Un seul niveau.** Pousser à nouveau sort. Pas de pile, donc pas de « où suis-je ».
8. **Une molette prêtée ne porte pas son contexte.** Pendant qu'elle affiche *Mode
   harmonique*, elle n'est plus Vélo : l'appui ne doit donc pas ouvrir le contexte de
   Vélo. Une molette porte le contexte de **ce qu'elle affiche**, jamais de ce qu'elle
   est. Aujourd'hui aucun paramètre prêté n'a de profondeur, donc l'appui ne fait rien
   — et le bouton l'annonce plutôt que d'agir de travers.
9. **Depuis un contexte ouvert**, seuls deux appuis répondent : celui de la molette
   poussée, qui ressort, et celui de `Row`, qui bascule vers le contexte de la row.
10. Une **marque permanente** sur les molettes qui ont une profondeur : on ne pousse
    jamais pour voir.
11. **Les contextes peuvent différer selon le mode d'affichage de la row** — travailler
    en `Contrôleur` ne demande pas les mêmes profondeurs que travailler en `Pas`. Ils
    doivent rester **analogues**, pour que la logique se transporte : ce n'est pas une
    permission de tout redéfinir, c'est la reconnaissance que la profondeur suit l'objet
    qu'on regarde.

> **Premier cas, décidé le 2026-08-15.** Une row affichée en **`Contrôleur`** met
> **`Destination` et `Interpolation`** sous `Valeur ▸ appui`. La même molette, sur une
> row en `Pas`, ouvre le voicing.
>
> C'est un **doublon assumé** : ces deux champs appartiennent à `PatternRow`, donc leur
> adresse systématique reste `Row ▸ appui` (§3.1). Le mode ne déplace rien — il
> *rapproche*. Quand on travaille sur un contrôleur, sa destination et son lissage sont
> ce qu'on règle le plus, et les chercher dans le contexte de la row serait un détour.
>
> La logique se transporte : `Valeur ▸ appui` veut toujours dire *« ce que devient cette
> valeur »*. Sur une note, comment elle se déploie en accord ; sur un contrôleur, où elle
> va et comment elle relie les pas.

Et une règle de forme, qui vaut pour tout le reste du document :

> **Rien n'est bloquant.** À un refus, on substitue un comportement par défaut
> raisonnable, jamais un message d'erreur. REC hors lecture fait du step-record ; sans
> profil, il enregistre le numéro de contrôleur brut. Le seul refus légitime est un
> **aiguillage** : ROLL sur une lane dit où les valeurs se règlent.

### Hiérarchie du Curseur, par vue

| Vue | Conteneur (molette 1) | Élément (molette 2) |
|---|---|---|
| PATTERN · ROLL · AUTO | la row | le pas — *push : entre dans le sous-pattern* |
| HARMONIE | la lane (accords ou tonalité) | le slot |
| SONG | la chaîne | le slot |
| GLOB | la row | — |

> **Pourquoi AUTO descend jusqu'au P-lock.** Sans ce niveau, la couche de base éditait
> `note`, `velocity` et `gate` sur la page des contrôleurs — des paramètres de notes.
> La tentation était de rendre les molettes dépendantes de la vue, ce qui aurait ruiné
> la règle 1. Le vrai diagnostic est ailleurs : **ce n'est pas la molette qui est mal
> réglée, c'est l'objet qui est mal choisi.** Le P-lock devenu objet courant, `Valeur`
> édite naturellement sa valeur de contrôleur, sans qu'aucune molette ne change de rôle.
>
> La preuve que le raisonnement tient : sur une **lane**, l'objet *est* déjà le
> contrôleur, et `Valeur` éditait déjà la bonne chose. La friction n'existait que pour
> les P-locks, enterrés dans un contexte au lieu d'être un niveau.
>
> **Périmé depuis le passage à six encodeurs** : ce raisonnement portait sur un niveau
> « P-lock » atteint par le Curseur. Avec une molette `Pas` dédiée dont le push fait
> entrer dans le sous-pattern, les P-locks n'ont plus d'adresse — c'est une régression
> ouverte, consignée au §6.

### Les huit boutons

| Bouton | Rôle |
|---|---|
| `ROW` · `HARMONY` · `PROJET` | **où on va** — re-appui = cycle dans la famille |
| `VUE` | **comment on regarde** — cycle ce que la vue courante affiche |
| `SHIFT` | note ↔ fonction des touches noires — `Page±` `Oct±` `Mes±` `Zoom±` |
| `PLAY` · `STOP` · `REC` | transport |

### Le clavier

27 touches, 16 blanches et 11 noires, disposées en piano. **Une seule règle décide de ce
qu'elles font**, et ce n'est pas la vue :

> **En saisie pas à pas, les blanches montrent et choisissent les pas. Sinon, elles
> jouent.**

C'est `REC` qui commande, pas la page. Les pas remplis se lisent donc **sur les touches,
sous les doigts**, sans quitter l'écran des yeux — ce qu'on veut précisément au moment
où l'on écrit. Et c'est le fonctionnement des machines Elektron : rien de nouveau à
apprendre.

`SHIFT` bascule les **noires** en fonctions — `Page±`, `Oct±`, `Mes±`, `Zoom±`.

*Ce que ça remplace* : une exception par vue — blanches = pas dans PATTERN, notes dans
ROLL — qu'il fallait connaître page par page.

*Ce que ça coûte, et qui s'assume* : dans PATTERN, `REC` désarmé, taper une blanche joue
la note au lieu de basculer le pas. Il faut armer `REC` pour éditer la grille au clavier.
Cohérent : **le clavier édite quand on est en écriture, il joue sinon.**

---

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
`Row ▸ appui`, **et elle ne change pas selon la page**. Sans quoi la
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
- **Destination et Interpolation ne vivent pas sur AUTO** mais sur la molette `Row`, puisque
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
| `numSteps` `ccNumber` `ccInterp` | `PatternRow` | `Row` ▸ appui |
| `harmonyMode[bar]` — **par mesure** | `PatternRow` | `Row` ▸ appui |
| `channel` | `PatternRow` | page PROJET |
| `muted` | `PatternRow` | **sans adresse** — voir §5.3 |
| `note` `velocity` `gate` `enabled` `span` `accent` `swingEnable` | `StepData` | couche de base, Vélo ▸ appui, Gate ▸ appui |
| `subPatIdx` | `StepData` | s'établit en entrant : `Pas` ▸ appui |
| `ccLocks[8]` | `StepData` | **écart de modèle, pas trou d'adresse** — voir §5.6 |
| `numSteps` `relativeToHost` | `SubPattern` | dans le sous-pattern |
| `duration` `advanceProgOnEnd` | `SubPattern` | **sans adresse** |
| `degree` `quality` (6) `extensions` `bassOffset` `durationBeats` | `ChordSlot` | HARMONIE |
| `rootPc` `scaleId` `durationBeats` | `KeySlot` | HARMONIE, lane tonalité |
| `harmonyEnabled` `followMasterTonality` `followProgression` `advanceProgOnSubPatternEnd` | `PatternHarmonySettings` | `Lane` ▸ appui |
| `op` `param1` `param2` | `ChainSlot` | SONG |
| `bpm` `timeSignature` `ccRateBudget` `songMode` `activePatternIndex` | `SequencerEngine` | Master ▸ appui, SONG ▸ appui |

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
| **Row** ou **Pas** | quels pas sont actifs — densité, euclidien, rotation |
| **Valeur** | les hauteurs — ambitus, marche, aléa, contrainte de gamme |
| **Vélo** | les vélocités — aléa, courbe, accentuation |
| **Gate** | les durées — legato/staccato, aléa |

Deux conséquences qui plaident pour cette forme :

- **« Pour chaque niveau » vient gratuitement.** La molette poussée choisit déjà le
  niveau : `Row` remplit la row, `Pas` remplit le sous-pattern du pas, `Lane` remplit
  la progression.
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
contour de ce qu'elle module — la row en automation son profil, une row de notes son P-lock. PATTERN
devient une table de mixage.

Deux notes qui vont ensemble :

- **Garantie.** Une préférence d'affichage ne peut rien corrompre, et c'est la règle 1
  qui l'assure : *la vue ne change que ce qu'on voit, jamais ce que la main fait*. C'est
  ce découplage qui rend des dispositions sauvegardables inoffensives.
- **Prudence.** « On le rendra configurable » est le moyen le plus élégant d'éviter de
  choisir un défaut. Le mode *Pas* doit rester bon pour qui ne touchera jamais au
  réglage.

**Le choix est PAR ROW** *(décidé le 2026-08-15)*. Patrice tient à voir une automation
occuper une rangée au milieu des rows de notes — donc l'affichage se règle row par row,
pas globalement.

> **`VUE` change l'affichage de la row sélectionnée. `Row` choisit laquelle.**
> **`⇧ + VUE` applique le même mode à toutes les rows**, pour retrouver la vue
> d'ensemble — comparer les accents de toutes les pistes d'un coup reste utile. Aucun
> contrôle nouveau : le modificateur existe déjà.

**Rangé dans `PatternRow`**, donc **sauvegardé avec le projet** : on rouvre, les rows
sont affichées comme on les avait laissées. Un affichage qu'il faut refaire à chaque
session est un affichage qu'on n'utilise pas.

**Quelle automation une row montre-t-elle en mode `Contrôleur` ?** La première qu'elle
porte. Choisir parmi les huit est la question du sélecteur, gardée pour après la vue
PATTERN (§5.6).

**Accès** : le bouton `VUE` (§2), obtenu en descendant `EXPORT` sur la page PROJET. Ce que `VUE` cycle sur ROLL, AUTO et
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

Volume et panoramique retombent donc sur la même réponse que le glide (§5.5) : des **CC
connus, nommés par le profil d'appareil**. Aucun paramètre nouveau, aucun moteur à
toucher. Une vue de mixage est surtout **une lecture de ce qui existe déjà**, plus le
mute — ce qui la rend étonnamment bon marché.

**Ce qu'elle explique, et qui n'est pas une régression.** Les `ccLocks[8]` semblent
avoir perdu leur adresse depuis que l'appui sur `Pas` sert à entrer dans le
sous-pattern. **C'en est une lecture fausse** : le code est resté à mi-chemin entre deux
modèles. Il a encore des P-locks — huit par *pas*, distincts de la lane de la row —
pendant que la conception a **une automation par row, avec huit slots**. Le glossaire a
d'ailleurs déjà supprimé le mot.

Ce n'est donc pas une adresse à retrouver, c'est une **migration à faire**. Dans le
modèle unifié, tout est en place : la row porte la destination et l'interpolation de
chaque slot, PATTERN les montre par le mode d'affichage, AUTO les édite pas par pas, et
l'appui sur `Pas` reste libre pour le sous-pattern — la profondeur *temporelle* du pas,
qui n'a rien à voir avec l'automation, laquelle appartient à la row.

**Une seule question reste ouverte** : qui choisit parmi les huit slots. Sur AUTO,
`Vélo` et `Gate` sont libres — une valeur de contrôleur n'a ni vélocité ni articulation
— donc l'une des deux ferait le sélecteur sans rien déloger. **À trancher après la vue
PATTERN**, pour ne pas mêler les deux chantiers.

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

### 5.4 L'articulation comme intention *(idée de Patrice, 2026-08-15 — après release)*

**Un séquenceur devrait gérer l'articulation, et aucun ne le fait.** Le reproche est
juste, et la cause est nommable : **les séquenceurs stockent l'approximation au lieu de
l'intention**. On ne note pas *staccato*, on note *gate 40 %*. La traduction est faite
une fois pour toutes à la saisie, puis perdue — change de synthé, elle n'est plus bonne,
mais il ne reste que le pourcentage.

C'est le même raisonnement que le micro-décalage du §5.5 : garder ce qui a été voulu
plutôt que sa version déjà traduite.

Les logiciels de partition ne s'en sortent pas mieux. MuseScore applique **une seule
traduction pour tous les instruments** — staccato → 50 % de la valeur, accent →
vélocité majorée — alors que le staccato est un coup d'archet ici, un relâché de touche
là, une enveloppe ailleurs. D'où ce rendu mécanique qu'on reconnaît aussitôt.

**Ce que NiDMI a et que les autres n'ont pas : le profil d'appareil.** Il nomme déjà les
CC d'un synthé ; il pourrait porter la **traduction de l'articulation** pour cet
appareil — de combien raccourcir, de combien pousser la vélocité, quel CC toucher s'il
expose son enveloppe.

Le pas stockerait une **intention** — *tenuto*, *staccato*, *marcato*, *legato* — et le
profil la réaliserait. Même mécanisme que le glide en CC 5/65 et le mixage en CC 7/10 :
une notion musicale, une traduction qui appartient à l'appareil. Change de synthé, la
même séquence se rejoue juste.

*Limite honnête* : sur du MIDI les leviers restent la durée, la vélocité, le
recouvrement et les CC. On n'invente pas une articulation qui n'existe pas. **Le gain
n'est pas d'avoir de nouveaux leviers, c'est de ne pas jeter l'intention.**

*Différé* : à reprendre après une première release, dont on est loin.

### 5.5 Enregistrement des notes entrantes *(décidé le 2026-08-14, non implémenté)*

**Le séquenceur doit pouvoir enregistrer les notes entrantes.** Il ne le peut pas
aujourd'hui : `processBlock` ne capture que les Control Change, et le
`midiMessages.clear()` jette tout le reste — les notes entrantes n'atteignent jamais le
moteur. Le step-record du clavier à l'écran fonctionne parce qu'il passe par l'éditeur,
pas par le flux MIDI.

#### Ce qui rend la quantisation difficile ici, et nulle part ailleurs

Quantiser sur une grille irrégulière n'est pas le problème : on divise la mesure par le
N de la row et on arrondit, qu'il vaille 16, 12 ou 5. Déduire `span` et `gate` de la
durée jouée ne l'est pas non plus.

Le problème est que **la grille est modifiable après coup**. Le N d'une row se règle en
tournant une molette, en pleine lecture — le moteur parle de *re-subdivision live*. Une
interprétation capturée dans une grille y est donc **figée** : on joue quelque chose de
juste sur une row en 16, on passe la row en 12 pour essayer, et le jeu est détruit sans
retour. Dans un séquenceur ordinaire ce cas n'existe pas, la grille ne bouge jamais.
Ici, c'est une fonction centrale.

S'ajoute que **le joueur ne sent pas forcément la grille de la row** : sur une row en 5,
un jeu en doubles-croches tombe systématiquement à côté. La quantisation ne corrige pas,
elle détruit.

#### La solution retenue : micro-décalage par pas, quantisation en option

Un **`int8_t` de plus dans `StepData`** : l'écart entre l'instant joué et le pas où la
note a été rangée. La capture le stocke toujours ; la lecture l'applique, donc
l'interprétation garde son grain.

**La quantisation devient une option** : activée, elle remet l'écart à zéro et la note
tombe pile sur la grille ; désactivée, l'écart est conservé et on rejoue ce qui a été
joué. Un seul champ, deux comportements, aucun mode caché.

*Précédent dans le modèle* : `swingEnable` est déjà un décalage temporel par pas — en
version booléenne. Le micro-décalage en est la forme continue.

*Coût mémoire*, vérifié : `StepData` fait 24 octets et n'a aucun rembourrage (tous ses
champs sont d'un octet). Le passage à 25 coûte **8 Kio par pattern**
(16 rows × 8 mesures × 64 pas), soit **136 Kio** pour le pattern actif et la banque de
16 — contre ~3,3 Mio déjà occupés par les pas. Négligeable, et cette zone vit en PSRAM
(`CAHIER §11.2`).

*Ce que ça ne sauve pas* : changer le N après la capture reste destructeur. Aucune
solution ne l'évite sans stocker le temps réel, ce qui serait une refonte du modèle —
`StepData` n'a aucun champ de temps, un index de pas porte une note.

#### Ce qui reste à trancher

- **Quelle row reçoit les notes.** La row sélectionnée est simple et prévisible ;
  router par `PatternRow.channel` permettrait d'enregistrer plusieurs pistes d'un seul
  geste, sans rien ajouter au modèle. Les deux se défendent.
- **Peut-on enregistrer dans un sous-pattern ?** Refuser rend le problème tractable — un
  sous-pattern est un acte d'édition délibéré, pas quelque chose qu'on improvise.
- **Où vit l'option de quantisation** : projet, row, ou par prise.
- **Les notes entrantes sont-elles retransmises en sortie ?** Un séquenceur qui les
  renvoie double-déclenche. L'usage veut qu'elles servent à la saisie sans être
  retransmises.

### 5.6 L'automation unifiée *(idée de Patrice, 2026-08-15)*

**Une row en automation et un P-lock sont le même mécanisme, rempli différemment.** Une
row en automation, c'est une valeur de contrôleur sur *chaque* pas, un seul CC,
interpolée. C'est-à-dire un P-lock rempli partout.

Il suffit que **l'interpolation devienne une propriété du slot** plutôt que de la row :

> Une row porte des notes **et** jusqu'à huit slots d'automation. Chaque slot a sa
> destination et son mode d'interpolation. Rempli partout, il donne ce qu'on appelait
> une lane ; rempli sur trois pas, ce qu'on appelait un P-lock.

**Ce que ça supprime**

- **Automatiser ne coûte plus une row.** Aujourd'hui on sacrifie une piste entière pour
  piloter un paramètre. Une row jouerait ses notes *et* modulerait huit contrôleurs.
- `RowKind` disparaît du modèle, comme `Type` a disparu de l'écran.
- Le piège de nommage disparaît : la valeur d'un contrôleur n'est plus rangée dans
  `StepData.note`.
- L'aiguillage de ROLL n'a plus lieu d'être — une row a toujours des notes.
- Et **P-lock** sort du glossaire, comme `lane` et `Type`. Trois termes supprimés par la
  même idée.

**L'affichage reste un choix par row.** Patrice tient à voir une automation occuper une
rangée dans PATTERN, au milieu des rows de notes — c'est pratique et il ne faut pas le
perdre. Mais c'est un besoin d'affichage, donc il se règle par l'affichage : chaque row
choisit de montrer ses notes ou le contour d'un de ses slots. On y gagne même, puisque
la row continue de jouer pendant qu'elle affiche son automation, et que plusieurs rows
peuvent montrer des slots différents.

**Mémoire : l'unification en fait GAGNER**, ce qui est contre-intuitif.

`StepCCLock` fait 2 octets — `ccNumber` et `value` — répétés 8 fois par pas. Or le
numéro de CC est en réalité une propriété du **slot**, donc de la row : le stocker à
chaque pas le duplique 8192 fois par pattern. Une fois remonté au niveau de la row, il
ne reste que la valeur, et `StepCCLock` tombe à 1 octet (0-127, un sentinelle pour
« absent »).

| | par pas | par pattern | ×17 patterns |
|---|---|---|---|
| aujourd'hui | 24 o | 192 Kio | **3,19 Mio** |
| unifié | 16 o | 128 Kio | **2,13 Mio** |

*(16 rows × 8 mesures × 64 pas = 8192 pas par pattern ; le moteur tient le pattern actif
plus une banque de 16.)*

Soit **~1 Mio économisé**, un tiers de la mémoire des pas. Le coût ajouté au niveau row
— 8 slots × {destination, interpolation} = 16 octets par row — pèse 4 Kio en tout.

**Ce que ça coûte vraiment** : une migration. Les rows aujourd'hui en `RowKind::CC` ont
leurs valeurs dans `StepData.note` et doivent passer dans `ccLocks[0]`. C'est un
changement de modèle du moteur, pas un aménagement d'interface.

**Piste ouverte au passage** : rien n'empêcherait alors une automation de pas de
s'interpoler entre les pas qui en portent une. `emitInterpolatedCC` sait déjà le faire.
C'est précisément là que la machine dépasse Elektron au lieu d'en hériter la limite.

### 5.7 Bibliothèque de cadences *(idée de Patrice, 2026-08-15)*

**Précision de vocabulaire d'abord** : une cadence n'est pas une suite d'accords, c'est
**la formule qui conclut une phrase** — la ponctuation, pas le texte. Parfaite (V → I à
l'état fondamental), imparfaite (la même affaiblie par un renversement), demi-cadence
(s'arrête sur V, une question), plagale (IV → I, celle de l'amen), rompue (V → VI, la
chute qu'on n'attendait pas).

`progression` reste donc le mot juste pour la suite d'accords du pattern.

**Mais la cadence est utile comme raccourci de composition.** Une petite bibliothèque
qu'on dépose dans la progression au lieu de poser les degrés un par un : un II-V-I, une
cadence rompue, une demi-cadence — et les accords s'écrivent avec leurs degrés.

Deux points d'ancrage existent déjà :

- `VISION §5.1ter` prévoit une *« future bibliothèque d'harmonies / progressions
  assignables »* : les cadences en sont le contenu naturel ;
- le **générateur** (§5.1) répartit des paramètres par niveau ; au niveau de la
  progression, ce qu'il y a à répartir, ce sont précisément des cadences.

C'est cohérent avec l'identité de la machine, qui pense déjà en degrés, en qualités et
en Da Capo : une notion que le musicien connaît, et que la machine lui rend.

### 5.8 Construire une progression géométriquement *(idée de Patrice, 2026-08-15 — à explorer)*

Une autre façon d'écrire une progression que de choisir des degrés un par un :
**se déplacer dans un espace** où la proximité vaut parenté harmonique.

Trois représentations connues, notées ici pour ancrer l'idée — rien n'est conçu :

- **le cercle des quintes** — circulaire, les voisins sont les tonalités parentes ;
- **le Tonnetz** (réseau d'Euler-Riemann) — un maillage où chaque triade est un triangle,
  et où deux triangles voisins partagent deux notes. On y avance par transformations
  minimales plutôt qu'en nommant des accords ;
- **les orbifolds de Tymoczko** — l'espace des accords où la distance *est* la conduite
  des voix.

Le Tonnetz est le plus proche d'une machine à encodeurs : deux axes, deux molettes,
et chaque cran est un enchaînement à voix conjointes. **À explorer**, pas à décider.

### 5.9 Les trois modes harmoniques, rendus distincts *(2026-08-15)*

Les modes `A` / `B1` / `B2` sont trop abstraits pour être compris, et surtout **`B1` ne
tient pas** : identique à `A` sur le degré I, presque une autre tonalité sur le VII, et
il contredit la qualité de l'accord qu'il prétend suivre.

**La cause, dans le code** : `B1` ne lit que le *degré*, jamais la *qualité*.

```cpp
effRoot = degreePitchClass(currentChord.degree, scaleId, rootPc);
return snapMidiNote(storedNote, scaleId, effRoot);   // scaleId = celui du MORCEAU
```

Sur un fa mineur emprunté en do majeur — la substitution plagale, cadence Fm → Do —
`B1` applique **fa majeur** : il autorise le La naturel contre le La♭ de l'accord.
Le frottement qu'on cherchait à éviter. `B2`, lui, reçoit bien `currentChord.quality`.

**L'axe qui rend les modes distincts** : *qu'est-ce qui décide des notes permises, et
combien en reste-t-il.*

| Retenu | Décidé par | Notes |
|---|---|---|
| **Chromatique** | rien | 12 |
| **Tonalité** | la gamme du morceau | 7, fixes |
| **Gamme de l'accord** | l'accord, **par sa qualité** | 7, mobiles |
| **Notes de l'accord** | l'accord | 3 ou 4 |

> ✅ **IMPLÉMENTÉ le 2026-08-15** — `nidmi-sequencer-core` `ad8d4e1`, VST `4566fe9`.

**La modification retenue** : `B1` construit sa gamme depuis la **qualité** de l'accord,
comme `B2`. Sur le fa mineur, il donne fa mineur — La♭ permis, La naturel interdit — et
la mélodie suit l'emprunt. La **tonicisation est conservée** : chaque accord reste une
tonique temporaire, ce qui permet à une mélodie de prendre le Fa♯ d'un V en do majeur.

> **Correction d'une erreur de ce registre.** Une première rédaction annonçait que
> *Tonalité* et *Gamme de l'accord* donneraient le même résultat sur tous les accords
> diatoniques. **C'est faux** : la qualité ne dit pas *quel* mineur. Un ré mineur en do
> majeur est **dorien** — Si naturel — et reçoit ici le mineur naturel, Si♭. Distinguer
> dorien, éolien et phrygien demanderait de croiser le degré et la gamme mère ; ce n'est
> pas fait, et c'est consigné dans un test.
>
> Une tentative de ne changer de gamme *que sur un emprunt* a été écrite puis retirée :
> elle supprimait la tonicisation et cassait un test existant.

**L'affichage suit la même source.** `harmony::allowedPitchClasses()` expose le masque
des classes de hauteur permises par un mode ; le piano roll le lit au lieu de recalculer
la gamme mère de son côté — il ne montrait donc jamais ce que le mode autorisait. Même
règle que pour la géométrie de dessin et de clic, déjà consignée dans `CLAUDE.md` :
**deux calculs parallèles finissent toujours par diverger.**

**Limite connue** : cette substitution-ci est représentable parce qu'elle garde un degré
diatonique et ne change que la qualité. Un emprunt à fondamentale chromatique — un ♭VI,
La♭ majeur en do — ne l'est pas : `ChordSlot.degree` va de 1 à 7 dans la gamme mère.
C'est la question laissée ouverte par `VISION §5.2c`, *emprunt contre modulation*.

### 5.10 Autres propositions

| Proposition | Emplacement visé | Origine · état |
|---|---|---|
| **Probabilité d'émission** | Vélo, sur une lane CC | 2026-08-14 — retenue. Comble une molette libre : une valeur de contrôleur n'a pas de vélocité. |
| **Glide** | Interpolation, sur une row de notes | 2026-08-14. Remplit un emplacement mort : `ccInterp` ne s'applique qu'aux lanes, l'encodeur affiche `—` sur une row de notes. **Forme retenue** : pas un paramètre nouveau, mais **CC 5 / CC 65 nommés par le profil d'appareil**. Le portamento est le travail du synthé ; générer le glissando nous-mêmes imposerait du pitch bend, qui est par canal et entre en collision avec le modèle de notes. |
| **Ancrage · Densité · Disposition** | Valeur ▸ appui, row Note | Le voicing, 4ᵉ pilier — `VISION §5.2b`. Exige plusieurs `NoteOn` simultanés : **refonte du moteur**, pas un module. |
| **Tension du lissage** | Valeur ▸ appui, lane CC | `CCInterp` est un enum sans paramètre. Coût faible. |
| **Aléa · Courbe de vélocité** | Vélo ▸ appui | Modulateurs — `VISION §6`. Recouvre en partie le §5.1. |
| ~~Ratchet~~ | — | **Retirée le 2026-08-15** : c'est un sous-pattern dont les sous-pas répètent le pas hôte. Rien à ajouter. |
| **Emprunt vs modulation** | Curseur ▸ appui, slot d'accord | `VISION §5.2c` — explicitement non tranché. |
| **Swing global** | Master ▸ appui | Le swing réel vit au niveau du sous-pattern. À unifier ou à renommer. |
| **Boucle de la chaîne** | Curseur ▸ appui, SONG | `ChainSlot` n'a que `op`, `param1`, `param2`. |

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
7. **L'enregistrement des notes entrantes** (§5.4) : quelle row recoit, sous-patterns
   ou non, ou vit l'option de quantisation, et si les notes entrantes ressortent.
8. **La polyrythmie n'est plus le principe d'organisation de PATTERN.** Elle reste un
   outil local — un charley en 3 contre 4 n'a rien d'exotique — mais la vue s'organise
   désormais autour de *l'état de chaque row*. Conséquence : `CAHIER_DES_CHARGES_V1.md`
   §12 pose l'identité *« cœur de valeur = tuplets / polyrythmie + harmonie »*. Cette
   phrase doit être vraie ou révisée avant le gel.

---

## 7. Ce que ce document révise

- `CAHIER_DES_CHARGES_V1.md` §10.2 décrit des **encodeurs contextuels** dont « le sens
  dépend de la Vue active ». La grammaire du §2 pose l'inverse. Le cahier est
  **supersédé** sur ce point.
- `VISION_ERGO_HARMONIE.md` §2.3 place le changement de niveau sur le push du Curseur.
  Deux molettes de navigation dédiées le remplacent, et le push de `Pas` fait entrer
  dans le sous-pattern. **Le mot « grain » ne désigne plus un niveau de navigation** ;
  il est réservé à son sens musical, le relief d'une interprétation (§5.4).
- `VISION_ERGO_HARMONIE.md` §2.4 — *« toute richesse future passe par le PUSH, JAMAIS
  par un nouvel encodeur »* — est révisé par le sixième encodeur.
- **Le bouton `VUE` revient, avec un autre métier.** Le cahier d'origine
  (§11.3) listait *Play · Stop · Rec · Vue · Export* ; `VUE` y servait à changer de
  page. Ce rôle appartient désormais aux trois boutons de famille. `VUE` reprend le
  huitième bouton — libéré par `EXPORT`, descendu sur la page PROJET — pour dire
  **comment on regarde** et non plus où on va.
- La surface matérielle fait foi côté `nidmi-seq-hardware` : **5 encodeurs EC11,
  8 boutons PB86, écran ILI9488 4,0″ 480×320**, BOM gelée.
