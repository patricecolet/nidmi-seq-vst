# NiDMI Seq — plugin JUCE (VST3, AU, Standalone)

Séquenceur MIDI basé sur [`nidmi-sequencer-core`](../nidmi-sequencer-core) (même dossier parent que ce dépôt).

## Concept central (à retenir pour la suite)

NiDMI Seq repose sur un **nouveau principe de grille** par rapport à un séquenceur « pas = fraction de noire » classique :

1. **Signature de mesure** — On définit la mesure (ex. numérateur / dénominateur) : cela fixe **ce qu’est une mesure** en temps musical (durée d’une barre au tempo donné).

2. **Ancre sur le premier tick de mesure** — Le moteur se synchronise sur le **début de mesure** (premier instant de la barre, frontière où la mesure recommence). C’est la référence pour savoir **où** une mesure commence dans le flux temps réel (hôte, horloge MIDI, etc.).

3. **Subdivision de toute la mesure en pas** — On choisit un nombre de **pas** `N` entre **1 et 64**. La mesure entière est découpée en **N intervalles de même durée**.

4. **Égalité temporelle** — Chaque pas dure **exactement** `(durée d’une mesure) / N`. Mis **bout à bout**, les `N` pas couvrent **strictement la même durée qu’une mesure** : pas de dérive par rapport à la barre ; le pattern est **une subdivision homogène de la mesure**, pas une grille indépendante des barres.

En résumé : **une mesure = une fenêtre temporelle fixée par la signature et le tempo ; cette fenêtre est divisée en N pas égaux, alignés sur le premier tick de mesure.**

## Prérequis

- **CMake** ≥ 3.22  
- **Compilateur C et C++** (le projet déclare `LANGUAGES C CXX` pour JUCE)  
- **Git** (téléchargement de JUCE via `FetchContent` à la première configuration)  
- **Accès réseau** la première fois : clone de JUCE 8.0.6 depuis GitHub  

### macOS

- Xcode ou les **Command Line Tools**  
- Pour **AU** et signatures : build typique en **Release** ou **Debug** selon besoin  

### Windows

- **Visual Studio 2022** (charge de travail « Développement Desktop en C++ ») ou équivalent avec générateur CMake MSVC  

## Disposition des dépôts

CMake attend que `nidmi-sequencer-core` soit **voisin** de ce dossier :

```text
repo/
  nidmi-seq-vst/          ← ce dépôt
  nidmi-sequencer-core/   ← obligatoire (add_subdirectory relatif)
```

Si le core est ailleurs, adapte le chemin dans `CMakeLists.txt` (`add_subdirectory`).

## Configuration

Depuis la racine de **nidmi-seq-vst** :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Options utiles :

| Option CMake | Défaut | Rôle |
|--------------|--------|------|
| `NIDMI_SEQ_BUILD_AUDIO_PLUGIN_HOST` | `ON` | Ajoute la cible **AudioPluginHost** (hôte JUCE pour tester VST3/AU). Mettre à `OFF` pour alléger la config et les builds si tu n’en as pas besoin. |

Exemple sans l’hôte JUCE :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNIDMI_SEQ_BUILD_AUDIO_PLUGIN_HOST=OFF
```

## Compilation

Build par défaut (tous les formats du plugin : **AU**, **VST3**, **Standalone**) :

```bash
cmake --build build --parallel
```

Cible regroupant plugin + hôte de dev (si l’option ci-dessus est activée) :

```bash
cmake --build build --target nidmi_seq_dev_session --parallel
```

## Sorties

Les artefacts sont sous le dossier de build, par exemple **depuis la racine du dépôt** :

```text
nidmi-seq-vst/build/NidmiSeq_artefacts/<Config>/…
```

`<Config>` est en général **`Release`** ou **`Debug`** selon `-DCMAKE_BUILD_TYPE` (Makefile/Ninja). Avec le générateur **Xcode**, il faut aussi passer `--config Release` ou `--config Debug` au build.

| Format | Chemin complet type (macOS, Release, Makefile/Ninja) |
|--------|------------------------------------------------------|
| **Standalone** | `build/NidmiSeq_artefacts/Release/Standalone/NiDMI Seq.app` |
| **VST3** | `build/NidmiSeq_artefacts/Release/VST3/NiDMI Seq.vst3` |
| **AU** | `build/NidmiSeq_artefacts/Release/AU/NiDMI Seq.component` |

Le nom de l’app contient un **espace** (`NiDMI Seq.app`) : en ligne de commande, mets des guillemets, par ex. :

```bash
open "build/NidmiSeq_artefacts/Release/Standalone/NiDMI Seq.app"
```

(toujours depuis le répertoire `nidmi-seq-vst`.)

**Important :** avec **`COPY_PLUGIN_AFTER_BUILD TRUE`**, seuls le **VST3** et le **AU** sont recopiés dans `~/Library/Audio/Plug-Ins/…`. Le **Standalone n’y apparaît pas** : il reste uniquement sous `build/…/Standalone/`, sauf si tu le copies toi-même (par ex. dans `/Applications`).

Si le dossier `Standalone` est vide ou absent après un build partiel, force la cible JUCE :

```bash
cmake --build build --target NidmiSeq_Standalone --parallel
```

Si **AudioPluginHost** est activé, l’application se trouve sous :

`build/nidmi_AudioPluginHost/AudioPluginHost_artefacts/<Config>/AudioPluginHost.app` (macOS).

## Tester rapidement

1. **Standalone** : ouvrir `NiDMI Seq.app` depuis le dossier `Standalone` ci-dessus (transport intégré, pas de DAW).  
2. **Comme dans une DAW** : lancer **AudioPluginHost** et charger le VST3 ou l’AU (depuis le dossier `build` ou depuis `~/Library/Audio/Plug-Ins/…` après install automatique).  

### Interface plugin

L’éditeur reproduit le **panneau matériel visé** : écran couleur (pages **PAT / ROLL / HARM / AUTO / GLOB / SONG**), encodeurs poussés, boutons de transport et **clavier** 16 blanches + 11 noires avec Shift.

> **Panneau cible, figé** (`nidmi-seq-hardware/docs/BOM.md`) : écran ILI9488 4,0″ **480×320**, **5 encodeurs EC11**, **8 boutons PB86**, 27 touches capacitives + ruban. Le VST en implémente bien **5**, mais leurs rôles sont contextuels au lieu d’être fixes.
>
> Référence ergonomique : **`VISION_ERGO_HARMONIE.md`**. Les descriptions « écran OLED / deux encodeurs » puis « ~320×240 / 4 encodeurs » de `CAHIER_DES_CHARGES_V1.md` §10.1–10.2 sont **périmées**.

Les paramètres (BPM, signature, horloge MIDI, etc.) restent dans l’**APVTS** et peuvent être automatisés par l’hôte.

### Profils de synthé

Le plugin charge des **profils** décrivant un synthé cible — carte des CC, libellés
courts, géométrie du panneau, valeurs par défaut — depuis
`~/Documents/NiDMI/Profiles/*.json`. Le profil actif se choisit sur la page **GLOB**.

Deux profils sont fournis dans [`Profiles/`](Profiles/) : **Kobol Expander**
(généré depuis le firmware MIDI-CV, cf. dépôt `synth`) et **Waldorf M**. Le format
est documenté dans [`Profiles/FORMAT.md`](Profiles/FORMAT.md) et reste éditable à
la main.

Un **MIDI learn** permet de remapper le CC reçu vers le CC envoyé au paramètre, sans
perdre le numéro d'origine : « Reset mappings » (page GLOB) revient à l'identité.

### Transport

**Play** et **Stop** pilotent le moteur (sauf si l’**horloge MIDI** est active : voir les paramètres `useMidiClock` / automation). L’**état** et le **BPM** affichés à l’écran viennent du moteur / des paramètres.

### Standalone : horloge MIDI

En **Standalone**, pas de playhead DAW. Active le paramètre **« Transport horloge MIDI »** (`useMidiClock`) via l’automation de l’app Standalone ou un contrôleur si exposé : le séquenceur peut suivre **Start / Stop** et **Timing Clock** sur l’**entrée MIDI**. Sinon, **Play / Stop** manuels. Régle un **périphérique MIDI d’entrée** dans les réglages audio du Standalone JUCE.

La **signature** et les autres réglages restent pilotables par **paramètres** / **état** du plugin, pas par une page « Sync » dans l’UI actuelle.  

## Dépannage

- **L’app Standalone n’a pas la nouvelle interface** : recompiler explicitement la cible **`NidmiSeq_Standalone`**, fermer l’ancienne app (Dock / Cmd+Q), puis ouvrir le **`.app` dans ce dépôt** avec un chemin **absolu** ou en étant dans `nidmi-seq-vst` : `open "build/NidmiSeq_artefacts/Release/Standalone/NiDMI Seq.app"`. macOS peut sinon rouvrir une autre copie (autre dossier de build, ancien export).  
- **Je ne vois pas le Standalone** : il n’est **pas** installé dans `~/Library` avec les plugins ; ouvre le dossier `build/NidmiSeq_artefacts/<Config>/Standalone/` dans le Finder ou lance la commande `open "…/NiDMI Seq.app"` ci-dessus. Vérifie aussi que `<Config>` correspond bien à ton build (Release vs Debug, ou `--config` avec Xcode).  
- **Échec du clone JUCE** : vérifier le réseau, les proxies, et que Git est disponible dans le PATH.  
- **CMake / langage C** : ne pas retirer `C` du `project(… LANGUAGES C CXX)` ; JUCE s’appuie dessus.  
- **Core introuvable** : vérifier que `../nidmi-sequencer-core` existe par rapport à ce dépôt.  

## Licence

Respecter la licence **JUCE** (et AGPL / commerciale selon ton usage) ainsi que celle de **nidmi-sequencer-core** et de ce dépôt si elle est précisée ailleurs.
