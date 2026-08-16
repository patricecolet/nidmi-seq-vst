# Format de profil d'appareil — schema 2

Un profil décrit **un synthé** : ses paramètres, leurs numéros de CC, et où se
trouvent leurs contrôles sur la face avant. C'est un fichier JSON, lu à
l'exécution — ajouter un synthé ne demande aucune recompilation.

```
~/Documents/NiDMI/Profiles/
├── kobol-expander.json
├── oberheim-....json
└── moog-....json
```

Le dossier est créé au premier lancement, avec le Kobol dedans en exemple.
Copier ce fichier est la façon la plus simple de décrire un autre synthé.

## Pourquoi un format et pas une image

Une photo de face avant ne suffit pas, et ne convient pas partout :

| | Éditeur bureau | Matériel 320×240 |
|---|---|---|
| Place | quelques centaines de pixels par section | 320 px pour **tout** |
| Photo du Kobol (ratio 3,18:1) | confortable | 320×101, **58 % de hauteur perdue** |
| Un potard | ~60 px, manipulable | **12 px** |
| Un label | lisible | **1,1 px** — illisible |
| Écran tactile | souris | **non tactile**, encodeurs |

D'où le principe : **la géométrie est décrite en coordonnées normalisées, pas
en pixels, et l'image est facultative.** Le bureau dessine la photo et pose les
potards dessus ; le matériel ignore l'image et dessine une section à la fois,
en s'appuyant sur `section` et `pos`. Un seul fichier, deux rendus.

## Structure

```json
{
  "schema": 2,
  "name": "Kobol Expander",
  "manufacturer": "RSF",

  "panel": {
    "image": "kobol-expander.jpg",
    "aspect": 3.18,
    "sections": [
      { "id": "vcf", "name": "VCF", "rect": [0.51, 0.05, 0.15, 0.90] }
    ]
  },

  "parameters": [
    {
      "cc": 74, "short": "Cutoff", "name": "VCF Cutoff",
      "group": "VCF", "section": "vcf",
      "type": "knob", "pos": [0.539, 0.285], "size": 0.045,
      "wired": true, "learn": null
    }
  ]
}
```

### Champs

| Champ | Rôle |
|---|---|
| `name` | **obligatoire.** Identifie le profil ; c'est lui qui est sauvegardé dans le projet, pas un index — ajouter un profil ne doit pas renommer les CC d'un projet existant |
| `voices` | combien de notes cette destination peut jouer à la fois. **Défaut 1** — un profil qui ne le déclare pas est traité comme monophonique. Borne la densité du voicing |
| `panel.image` | facultatif. Absent → rendu schématique, sans photo |
| `panel.aspect` | largeur ÷ hauteur du panneau |
| `panel.sections[].rect` | `[x, y, w, h]` normalisés. Une section = **une page** sur le matériel, **une zone** sur le bureau |
| `cc` | 0–127. Hors plage → l'entrée est ignorée |
| `short` | ≤ 7 caractères. Les pastilles de la page AUTO font ~28 px en police 10 |
| `type` | `knob`, `switch`, `selector` — ce que le rendu dessine |
| `pos` | `[x, y]` normalisés **sur le panneau entier**, centre du contrôle. `null` = pas de contrôle physique |
| `size` | diamètre, en fraction de la **largeur** du panneau |
| `wired` | le paramètre a-t-il un effet **audible aujourd'hui** ? |
| `learn` | CC entrant **supplémentaire**. `null` = aucun. Le CC propre du paramètre passe de toute façon à l'identique |
| `bipolar` | le paramètre va de −64 à +63, remappé en 0–127 en ajoutant 64. L'éditeur doit l'afficher **signé** |

### `voices` borne le voicing

Le moteur émet jusqu'à `kMaxVoicing` notes par pas et ne sait pas combien la
destination peut en tenir. Le profil le dit, et **l'utilisateur connaît sa machine** :
c'est une valeur qu'on règle une fois, pas un calcul à faire faire au séquenceur.

À une voix, densité et ancrage **n'apparaissent pas** : pas de refus, pas de grisé, le
contexte n'existe simplement pas sur cette row.

Sur un multitimbral, les voix se répartissent entre les parties — le Waldorf M en
déclare 8, ce qui est la bonne valeur pour l'usage courant. Si tu le configures
autrement, tu changes le chiffre dans le JSON.

La règle vaudra pour les autres capacités du profil : une carte de percussion fera
disparaître le voicing, la gamme et le mode harmonique, parce que sur une boîte à
rythmes les notes sont des instruments et non des hauteurs.

### `wired` n'est pas « décrit dans la table »

C'est **« produit un son aujourd'hui »**. Sur le Kobol, le matériel actuel est
un Teensy 2.0 avec un seul MCP4822, donc deux sorties CV : le pitch et le
cutoff. Cas non évident : `CC 116` (vélocité → VCA) vaut `false`, parce qu'il
module le VCA Sustain, qui n'a pas de sortie. Le CC part, il ne produit rien.

Sur 21 contrôleurs du Kobol, **7 sont audibles**.

### `learn` ajoute une source, il n'en remplace pas

**Le CC propre d'un paramètre passe toujours à l'identique** : le profil déclare
`cc: 74`, un CC 74 entrant ressort en CC 74, sans qu'on ait à l'écrire. Inutile
donc de remplir 21 champs redondants, et un profil écrit à la main sans aucun
`learn` fonctionne d'emblée.

`learn` ne sert qu'à **ajouter une seconde source** :

```json
{ "cc": 74, "name": "VCF Cutoff", "learn": 21 }
```

CC 21 **et** CC 74 pilotent alors le cutoff. Apprendre un potard de contrôleur
ne fait donc jamais perdre le numéro d'origine du synthé.

Un `learn` qui vise le CC propre d'un autre paramètre le détourne : l'explicite
l'emporte sur l'identité.

Seuls les CC **déclarés dans le profil** traversent. Le plugin ne devient pas un
MIDI thru : un CC inconnu reste jeté, comme avant.

### `learn` : deux familles de synthés

| | Le synthé a une sortie MIDI | Le synthé n'en a pas |
|---|---|---|
| Exemples | Oberheim Matrix, Moog récents, tout moderne | **Kobol**, la plupart d'avant 1983 |
| La carte CC | imposée par le constructeur | **choisie par nous**, dans le firmware |
| Learn possible | oui : bouger un potard du synthé, il apprend | non : le synthé n'émet rien |

Sur un synthé muet, `learn` ne peut donc signifier qu'une chose : **lier un
potard de ton contrôleur à un paramètre**. C'est du mapping d'entrée, pas de la
découverte. Le champ marche pour les deux familles, mais le geste qui le
remplit diffère.

## Coût mémoire — pour le portage ESP32

Sur bureau la question ne se pose pas. Sur ESP32-S3 elle mérite un chiffre,
d'autant qu'il est **borné** : un profil décrit des CC, et MIDI n'en définit que
**128**. Le pire cas est donc calculable, sans connaître le synthé.

Coût d'un profil en RAM, structure embarquée compacte (pas de `juce::String`) :

| Paramètres | `char[]` fixes, 48 o/param | blob + offsets, 16 o/param |
|---|---|---|
| 21 — Kobol Expander | 1,0 Ko | 0,8 Ko |
| 47 — Waldorf M *(relevé sur son manuel)* | 2,2 Ko | 1,9 Ko |
| **128 — plafond MIDI** | **6,0 Ko** | 5,1 Ko |

Pour une collection entière :

| Stratégie | 10 synthés | 50 | 128 |
|---|---|---|---|
| Tout charger au démarrage (ce que fait le VST) | 30 Ko | 150 Ko | **384 Ko** |
| Charger le profil actif + les noms pour le sélecteur | 3,3 Ko | 4,6 Ko | **7,0 Ko** |

**Un seul pattern de la grille pèse 205 Ko**, soit 34 profils au plafond MIDI.
Même la stratégie naïve tient dans 4,7 % des 8 Mo de PSRAM d'une N16R8. Le
chargement paresseux n'est utile que sur une carte **sans** PSRAM.

### `bipolar` : sinon « 64 » s'affiche pour un réglage centré

Beaucoup de synthés exposent des paramètres signés — détune, quantité
d'enveloppe, suivi de clavier — sur une plage −64…0…+63, remappée en 0–127 par
ajout de 64. Waldorf les marque d'un astérisque dans ses manuels.

Sans ce champ, l'éditeur afficherait `64` là où l'utilisateur attend `0`, et
`0` là où il attend `−64`. Le MIDI émis est inchangé : c'est purement de
l'affichage.

## Profils fournis

| Fichier | Synthé | Paramètres | Source |
|---|---|---|---|
| `kobol-expander.json` | RSF Kobol Expander | 21 | **généré** depuis la carte MIDI du firmware |
| `waldorf-m.json` | Waldorf M | 47 | relevé sur le manuel, appendice p. 82 |

Le profil du M vaut aussi pour les **Microwave II / XT** : son manuel précise
qu'il en reprend la disposition CC classique, à l'ajout près du CC 63.

Il illustre deux choses que le Kobol ne montre pas — un profil **sans image**,
et un synthé qui **émet** du MIDI quand on bouge ses potards, donc où le vrai
learn est possible.

## Rester d'accord avec la machine — `sync`

Le problème central de tout éditeur de synthé matériel : **l'utilisateur charge
un preset sur la machine, et les potards de l'éditeur ne correspondent plus à
rien.** Il n'y a pas de solution générale — ce que l'on peut faire dépend
entièrement de ce que l'appareil sait dire de lui-même.

```json
"sync": {
  "ccOnEdit": true,
  "dump": "manual",
  "dumpFormat": "waldorf-microwave-1",
  "dumpOverwritesStoredProgram": true
}
```

| Champ | Ce qu'il change |
|---|---|
| `ccOnEdit` | l'appareil émet un CC quand on bouge un contrôle de façade. Si oui, l'éditeur **reste** synchrone après un premier accord, et le vrai MIDI learn est possible |
| `dump` | `none` · `manual` · `request` — voir ci-dessous |
| `dumpFormat` | le codage du dump, à décoder pour en tirer les valeurs |
| `dumpOverwritesStoredProgram` | **danger** : envoyer un dump écrase un preset mémorisé |

### Les trois cas de `dump`

**`none`** — rien à lire. Le Kobol n'a aucune sortie MIDI : il n'émet rien, ne
dumpe rien. **L'état de l'éditeur *est* la vérité**, il n'y a rien à
synchroniser. C'est le cas le plus simple, et celui de la plupart des synthés
d'avant 1983.

**`manual`** — l'appareil sait envoyer son état complet, mais seulement depuis
**son propre menu**. C'est le cas du Waldorf M : *System > Operations > Send
Current Sound (SYSEX)*. Son manuel ne documente **aucune demande de dump** —
l'éditeur ne peut donc pas l'interroger. Il doit demander à l'utilisateur de
déclencher l'envoi, puis décoder ce qui arrive.

**`request`** — l'éditeur demande, l'appareil répond. La synchronisation devient
invisible. C'est le confort que la plupart des synthés modernes offrent, et
qu'aucun de nos deux profils n'a.

### L'avertissement à ne pas perdre

Le manuel du M dit :

> *When a sound program sysex message will be received by M, it will be
> immediately overwrite the current selected sound program.*

Recevoir un dump **écrase un programme mémorisé**, pas seulement le tampon
d'édition. Un éditeur qui enverrait un dump à la légère détruirait les presets
de l'utilisateur. D'où le champ `dumpOverwritesStoredProgram` : la donnée doit
voyager avec le profil, pas rester dans un manuel PDF.

### Ce qu'un éditeur devrait en faire

| `dump` | `ccOnEdit` | Comportement raisonnable |
|---|---|---|
| `none` | non | l'éditeur fait foi ; à l'ouverture, envoyer son état |
| `manual` | oui | après un changement de preset, signaler « désynchronisé » et proposer la marche à suivre ; suivre ensuite les CC entrants |
| `request` | — | demander un dump à l'ouverture et à chaque Program Change |

Le repli universel, valable partout : le **soft takeover** — un potard de
l'éditeur n'agit qu'une fois qu'il a croisé la valeur courante. Il n'accorde
pas l'affichage, mais il évite le saut brutal.

## Synthés multitimbraux

Le format décrit **une partie**, pas un instrument entier. Sur un synthé
multitimbral — le Waldorf M, cible du cahier — le même CC sur un canal
différent adresse une autre partie.

Rien de spécial à prévoir : **c'est le canal de la row qui décide de la
partie**. Le même profil sert 4 rows sur 4 canaux, et le format reste
inchangé. Il n'a donc aucune notion de canal, délibérément.

## Coordonnées

Tout est normalisé `0..1` sur le panneau, **jamais en pixels**. Une position
reste donc valable quelle que soit la résolution de la photo et quelle que soit
la taille de l'écran. Pour placer un contrôle, mesurer sur la photo et diviser
par ses dimensions.

Le Kobol a ses potards sur deux rangées, à `y = 0.285` et `y = 0.705`.

## Droits sur les images

`panel.image` désigne un fichier **local**. Une photo de face avant appartient à
son auteur ou au constructeur : elle convient pour un usage personnel, pas pour
être redistribuée avec un plugin.

Pour distribuer, deux voies propres : ne livrer aucune image et s'en tenir au
rendu schématique — que `pos` et `sections` suffisent à produire — ou dessiner
son propre panneau. Le format fonctionne sans image, c'est délibéré.

## Ajouter un synthé

1. Copier `kobol-expander.json` sous un autre nom.
2. Changer `name`, `manufacturer`, la liste des `parameters`.
3. Sans photo : mettre `panel.image` à `null`, garder `sections` et `pos`.
4. Avec photo : mesurer les centres des potards, diviser par les dimensions.
5. Relancer le plugin — le profil apparaît dans **GLOBAL > Profil**.

Un fichier invalide est **rejeté, jamais deviné** : mieux vaut afficher
« CC 74 » qu'un mauvais libellé. Un paramètre sans `name`, ou dont le `cc` sort
de 0–127, est ignoré sans faire tomber le reste du fichier.

## Le profil du Kobol est généré

Ne pas éditer `kobol-expander.json` à la main : il est produit depuis la carte
MIDI du firmware, qui fait foi pour le firmware **et** pour le plugin.

```sh
cd ~/repo/synth/kobol-expander/midi-cv
python3 tools/gen_device_profile.py --write
```

Sans `--write`, le script signale une divergence et sort en 1. Recopier cette
table à la main avait déjà produit cinq désaccords en deux jours, dont deux
`wired` faux.
