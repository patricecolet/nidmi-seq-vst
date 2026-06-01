# NiDMI Seq — Vision ergonomique & harmonique

> Document de **vision cible**, issu de la session de design du 2026-06-01.
> Il décrit où va le projet, **distinctement de ce qui est déjà implémenté**.
> Deux couches sont explicitement séparées : **V1 RÉEL** (ce que le code fait
> aujourd'hui) et **VISION** (la forme voulue). Ne pas confondre les deux : le
> cahier ne doit jamais décrire une machine qui n'existe pas encore comme si
> elle existait.
>
> Complément de `CAHIER_DES_CHARGES_V1.md` (contrat d'implémentation V1).

---

## 0. Principe générateur unique

**« Une valeur séquencée pilote quelque chose de plus riche ; le *push* de
l'encodeur donne accès au "plus riche". »**

C'est le fil conducteur qui relie toutes les features, présentes et futures :
- la **zone chromatique** (une note inscrite → la note jouée après filtre harmonique) ;
- le **span** (un pas → plusieurs pas occupés) ;
- les **sous-patterns** (un pas → un tuplet imbriqué) ;
- le **voicing** (une note → un accord déployé) ;
- les **modulateurs** à venir (une valeur → une courbe / de l'aléatoire).

Conséquence ergonomique : **tourner = la valeur**, **pousser-tourner = la
profondeur** de cette valeur. Un seul geste à apprendre, valable pour tout
paramètre actuel ou futur. C'est ce qui protège l'ergonomie de la surcharge
quand le projet grandit.

---

## 1. Les piliers

### Piliers fondateurs (existants, voir cahier V1)
1. **Grille bar-relative homogène, par row** — chaque row divise la mesure en
   N pas ; réalignement au downbeat ; polyrythmie naturelle. *(implémenté)*
2. **Harmonie de premier ordre** — progression d'accords + modes de suivi par
   row (A / B1 / B2 / Chromatique). *(implémenté, mais schématique — voir §4)*
3. **Ergonomie hardware-first** — clavier 27 touches, encodeurs, écran. *(en refonte — voir §2, §3)*

### 4ᵉ pilier — VOICING / POLYPHONIE VERTICALE *(VISION, non implémenté)*
Complément **vertical** de la polyrythmie (horizontale). NiDMI doit jouer
l'harmonie, pas seulement la suivre. Voir §5 en détail.

> **Symétrie cible :** horizontal = polyrythmie (le temps, déjà fort) ;
> vertical = polyphonie/voicing (l'empilement, à construire).

---

## 2. Modèle d'interaction refondu *(VISION — révisé 2026-06, suite tests d'usage)*

### 2.0 Principe structurel : DÉCOUPLER la vue de la grammaire d'édition

**Diagnostic de fond** (révélé par l'usage : ajouter→éditer une note et naviguer
dans les sous-patterns sont pénibles). La cause n'est pas le nombre de gestes,
c'est que **la vue et la grammaire d'édition sont mariées** : changer de vue
change *comment on édite* (Enc1 = N en PATTERN mais = Note en ROLL ; le clavier =
pas en PATTERN mais = notes en ROLL ; la hauteur ne s'édite que dans une vue).
On voyage donc entre les vues pour une seule note, et chaque voyage réapprend une
grammaire. C'est aussi pourquoi on a tendance à tout empiler sur Shift.

**Règle structurelle :** *la vue ne change que ce qu'on VOIT ; la grammaire
d'édition reste identique partout.* PATTERN et ROLL = deux **lentilles** sur le
même objet (PATTERN montre le rythme, ROLL les hauteurs), édité **pareil** dans
les deux. Le clavier ne porte plus la grammaire : il sert à **jouer/saisir vite**
(step-record). Conséquence : Shift se vide presque entièrement.

### 2.1 Les canaux à sens stable

| Canal | Sens unique et stable |
|---|---|
| **Tourner un encodeur** | la **valeur** de SON attribut (même rôle dans toutes les vues) |
| **Push-and-turn** | la **profondeur** de cet attribut (grain, voicing, courbe…) |
| **Bouton de vue (+ re-appui)** | **où aller** (ce qu'on regarde, pas comment on édite) |
| **Shift** | **note ↔ fonction** des noires (= navigation au clavier, dispo PARTOUT) |
| **Double-clic** | **entrer dans le détail** de l'objet cliqué |

### 2.2 La navigation est un canal GLOBAL, jamais confisqué

Friction clé constatée : le drill-in d'un sous-pattern **réquisitionnait** les
touches (Back/Mode) → on perdait R±, Page±, Oct±, zoom, et on devait sortir du
sub pour changer de row. **Inacceptable.** Règle :
- **La navigation (row, pas, octave, zoom, mesure) reste accessible quel que soit
  le mode/vue/grain.** Via `Shift + noires` (= les fonctions de navigation), qui
  marche partout, y compris en drill-in.
- Aucun sous-état ne doit voler la navigation.

### 2.3 Le sous-pattern = un GRAIN, plus un mode

- **push-Curseur descend d'un grain : row → pas → sous-pas → (row).** Au grain
  sous-pas, l'encodeur Curseur navigue les sous-pas et les encodeurs éditent le
  sous-pas courant — **édité en place dans le ROLL** (mini-blocs déjà visibles),
  sans changer de vue, donc la navigation n'est jamais perdue.
- Le **drill-in plein écran reste, fort et bien accessible**, mais comme **zoom
  d'affichage optionnel** pour les sous-patterns denses (jusqu'à 16 sous-pas), pas
  comme le seul accès. En drill-in : navigation prioritaire ; Back = push-Curseur
  remonte d'un grain ; Mode rel/abs sur sa noire dédiée.
- C'est l'application directe du principe unificateur : **le sous-pas est la
  profondeur du pas** (comme le voicing est la profondeur de la note).

### 2.4 Règles transverses

- **Push réservé aux paramètres à faible course / aux profondeurs.** Un paramètre
  à grande course (vélo 0-127, défilement multi-octaves) garde le push pour une
  action *discrète* (toggle/reset), pas une valeur continue.
- **Double-clic = uniquement « zoomer dans l'objet »** (jamais une fonction
  étrangère, jamais une action critique/fréquente).
- **Note par défaut = hérite du dernier pas édité** (hauteur/vélo/gate), au lieu
  du C4/100/80 rigide actuel. Continuité musicale, moins de corrections.
- **Règle de scaling du hardware (impérative) :** *1 encodeur par attribut de
  BASE ; toute richesse future (modulateurs, courbes, voicing, aléa) passe par le
  PUSH de l'encodeur concerné, JAMAIS par un nouvel encodeur.* Le nombre
  d'encodeurs est ainsi borné.
- L'**harmonie habite sa vue** (territoire dédié) : elle ne déverse pas de
  logique dans la couche globale.

---

## 3. Schéma hardware cible *(VISION)*

### 5 encodeurs — UN ATTRIBUT PAR ENCODEUR, rôle stable dans TOUTES les vues

Chaque encodeur édite **toujours le même attribut** du pas (ou sous-pas) sous le
curseur, quelle que soit la vue. La vue ne change que l'affichage, pas le rôle des
encodeurs (cf. §2.0).

| Enc | Tourne (valeur) | Push-turn (profondeur) |
|---|---|---|
| **1 — CURSEUR** | se déplacer dans la séquence | **grain** : row → pas → sous-pas |
| **2 — HAUTEUR** | note du pas / sous-pas courant | **voicing** (ancrage / densité / disposition) |
| **3 — VÉLO** | vélocité 0-127 *(grande course)* | courbe / aléa de vélo *(action discrète)* |
| **4 — GATE** | articulation (durée %) | **span** (durée en nb de pas — familier : les deux = la durée) |
| **5 — MASTER** | **BPM, toujours accessible** | swing / volume global |

> - Le **CURSEUR** porte la navigation fine *dans* la séquence ; la navigation de
>   structure (row, octave, zoom, mesure) reste sur `Shift + noires`, globale (§2.2).
> - Le **MASTER** est transversal : le tempo n'est plus enfoui dans Global.
> - **Span en push de Gate** (décision : span édité ponctuellement, pas du même
>   rang qu'hauteur/vélo/gate ; gate et span sont tous deux « la durée » → push
>   familier, pas d'ambiguïté). Garde le hardware à 5 encodeurs.
> - Décompte des attributs de BASE : Curseur, Hauteur, Vélo, Gate, Master = 5.
>   Span, voicing, courbes, grain, aléa = des PROFONDEURS (push), pas des
>   encodeurs (règle de scaling §2.4).

### 8 boutons

- **3 boutons de vue** : `ROW` · `HARMONY` · `PROJET`
  - **re-appui** = cycle dans la famille (pas de bouton « niveau » séparé) :
    - `ROW` → Pattern → Roll → Auto
    - `PROJET` → Global → Song
    - `HARMONY` → vue dédiée (pilier transversal)
- **SHIFT** (note ↔ fonction des noires)
- **Play · Stop · Rec · Export**

### 27 touches (16 blanches + 11 noires) — disposition piano

- **ROLL** : clavier chromatique complet. Blanche = degré diatonique,
  **noire = altération** (le dièse au-dessus de sa blanche). *(implémenté)*
- **Shift OFF = jouer des notes / Shift ON = fonctions.** *(implémenté)*
- **PATTERN** : blanches = pas ; noires = fonctions directes (pas de note à jouer).
- Mapping des fonctions sur ⇧+noire (même touche = même fonction entre vues) :
  R± · Page± · Sub · Mes± · Oct± · Mode rel/abs. *(implémenté)*

---

## 4. Taxonomie des vues *(VISION partielle — accès à refondre)*

Critère de regroupement = **échelle de l'objet manipulé**, sauf Harmony qui est
**transversale** (ressource réutilisable entre patterns).

| Famille | Vues | Échelle |
|---|---|---|
| **ROW** | Pattern · Roll · Auto | une piste |
| **HARMONY** | Harmony | transversale (pattern + réutilisable) |
| **PROJET** | Global · Song | le projet |

- **Pattern et Roll = le même objet** (la note d'une row, deux focales :
  rythme / résultat tonal). Roll est l'**aval** : il montre la note *après*
  transformation harmonique. → un seul bouton, bascule rapide.
- **Harmony a un bouton dédié** non par hiérarchie mais par **nature** : c'est
  une ressource harmonique partageable entre patterns (« mêmes modulations sur
  des patterns différents »). Ouvre la voie à une future **bibliothèque
  d'harmonies / progressions** assignables.
- **Tonalité maître (root/scale) → rapatriée dans HARMONY** (socle tonal au
  même endroit que accords/modes). Global ne garde que le projet (BPM,
  signature, arrangement). *(VISION — aujourd'hui root/scale sont dans Global)*

---

## 5. HARMONIE — le pilier à approfondir *(mélange RÉEL / VISION)*

> L'harmonie est un pilier fondateur mais sa forme actuelle reste **schématique**.
> Cette section développe la cible. C'est la priorité conceptuelle du projet.

### 5.1 État RÉEL (implémenté)
- `ChordProgression` : jusqu'à 32 `ChordSlot` (degré I–VII + qualité +
  extensions + bassOffset + `durationSlots`).
- `durationSlots` **fonctionnel** : chaque slot dure N mesures (progression
  découplée du nombre de mesures du pattern).
- **Modes de suivi par row** : A (degré dans gamme mère), B1 (degré rerouté via
  l'accord courant), B2 (chord-tones), Chromatic (brut).
- **Vue HARMONIE** : chips d'accords (source des degrés), noms d'accords réels
  affichés, en-tête « Key », bande Rows (mode par row), Harmonie ON/OFF.
- **Zone chromatique** visualisée dans Pattern (teinte = note jouée après filtre).
- **Édition** : ⇧+blanche cycle le mode d'une row (délié→A→B1→B2) ;
  nouveau slot = copie du dernier.

### 5.2 VISION — ce qui manque ou reste schématique

**a) Lane de tonalité / modulations** *(le cœur manquant)*
Aujourd'hui : une seule tonalité maître figée pour tout le pattern.
Cible : une **lane de tonalité dédiée** = suite de marqueurs root+gamme avec
durées (calés sur la mesure, libellés `Cmaj→Gmaj`), permettant des
**modulations** dans le temps. `resolveDegreeToMidi` prend déjà scaleId/rootPc
→ lui passer la clé courante de la lane au lieu du master figé.
*(Issu de l'étude comparative deep-research : métaphore gagnante = lane dédiée,
texte explicite, pose en ≤2 gestes. Anti-pattern : LED/couleur seule.)*

**b) Voicing / polyphonie verticale** *(4ᵉ pilier — détail)*
Le voicing est une **donnée inhérente au pattern** : il indique *comment jouer
l'harmonie*. Une row « accordée » génère **plusieurs notes** depuis l'accord
courant, réglées par :
- **Ancrage** = rôle de la note inscrite dans l'accord :
  1. note la plus **haute** (sommet, accord déployé dessous),
  2. note la plus **basse** (basse, déployé au-dessus),
  3. **indépendante** (note libre, accord ajouté autour) ;
- **Densité** = nombre de notes (2, 3, 4…) ;
- **Voicing** (sens strict) = ordre / disposition (close, drop, spread…).
- **Rendu** : les notes générées apparaissent **en filigrane dans le Roll**
  (pleines = inscrites/pilotées, filigrane = ajoutées par le voicing) — version
  verticale du « montrer le résultat de la transformation ».
- **Accès** : push-and-turn sur la note (Enc1-push). Si surcharge → alléger plus tard.

**c) Emprunt vs modulation** *(à décider, V1.5 ?)*
Distinguer un emprunt (`bVI`, reste dans la clé) d'une vraie modulation
(marqueur de clé sur la lane). Non tranché.

### 5.3 Impact moteur du voicing *(le plus profond à venir)*
Aujourd'hui le core émet **strictement 1 note / pas / row**. Le voicing exige
**plusieurs NoteOn simultanés** → touche `kMaxActiveNotes`, la gestion des
NoteOff, l'allocation, le MIDI out, et l'interaction avec span + sous-patterns.
**Plus lourd que le multi-mesures.** À traiter comme une refonte de fondation,
pas un module ajouté.

---

## 6. Modèle à couches d'une « row » *(synthèse VISION)*

Une row n'est pas une séquence de notes, c'est un objet à **couches empilées** :

1. **La note inscrite** (le pas). *(réel)*
2. **Le rôle harmonique** qui la transforme (Harmony → degré / chord-tone). *(réel)*
3. **Les modulateurs par paramètre** — courbes (hauteur, vélo), aléatoire. → chaque
   paramètre a une *profondeur* (valeur + modulation), accessible au push. *(vision)*
4. **Le voicing / polyphonie** — la note pilote un accord (voir §5.2b). *(vision)*

La chaîne d'affichage : **Pattern → Harmony → Roll** (Roll = aval, montre le
résultat transformé).

---

## 7. Méthode de travail

- **Fixer les canaux maintenant, différer le détail.** Les 5 canaux (§2) sont
  robustes quelles que soient les features ajoutées ; le détail UI de chaque
  feature se décide à son implémentation.
- **Valider par l'usage, pas seulement par la logique.** La cohérence
  conceptuelle ne remplace pas une vraie séquence jouée de bout en bout. Test
  prioritaire : faire un morceau avec l'existant et noter les frictions.
- **Réserver l'espace harmonie** = garder les vues comme conteneurs autonomes
  (l'harmonie vit dans sa vue), ne pas re-déverser de logique harmonie dans la
  couche globale.

---

## 8. Écart vision / implémentation — à surveiller

| Élément | État |
|---|---|
| Grille bar-relative, polyrythmie | ✅ implémenté |
| Multi-mesures (1–4), span, sous-patterns | ✅ implémenté |
| Harmonie de base (prog, modes, durationSlots) | ✅ implémenté |
| Grammaire touches (note/fonction), modes rel/abs | ✅ implémenté |
| Zone chromatique visualisée | ✅ implémenté |
| Découplage vue / grammaire d'édition (§2.0) | ❌ vision (aujourd'hui : grammaire suit la vue) |
| Grammaire stable : 1 attribut par encodeur, partout | ❌ vision (aujourd'hui : Enc contextuels) |
| Navigation = canal global, jamais confisqué (§2.2) | ❌ vision (aujourd'hui : drill-in vole la nav) |
| Sous-pattern = grain (push-curseur), édition en place ROLL | ❌ vision (aujourd'hui : drill-in = mode exclusif) |
| Note par défaut hérite du dernier pas édité | ❌ vision (aujourd'hui : C4/100/80 rigide) |
| 5ᵉ encodeur master | ❌ vision |
| Boutons de vue par famille + re-appui | ❌ vision (aujourd'hui : cyclage « Vue ») |
| Push-and-turn généralisé (profondeur) | ❌ vision (aujourd'hui : ⇧Enc partiels) |
| Tonalité maître dans Harmony | ❌ vision (aujourd'hui : Global) |
| Lane de tonalité / modulations | ❌ vision (cœur manquant) |
| Voicing / polyphonie verticale | ❌ vision (4ᵉ pilier, refonte core) |
| Modulateurs / courbes / aléatoire | ❌ vision |

> **Règle d'or :** ne jamais laisser le cahier décrire le ❌ comme du ✅.
