# Charte graphique — états d'un slot (pas)

> Référence des codes visuels d'un pas dans l'écran TFT (vues PATTERN / ROLL).
> But : que chaque état soit reconnaissable **même cumulé** avec d'autres.
> Principe directeur : **un état = un CANAL visuel** (remplissage / luminosité /
> liseré / coin-badge), pas une couleur ajoutée au hasard.
> Couleurs définies dans `Source/HardwareStyleComponents.cpp` (palette en tête).

## Les canaux (pour éviter les collisions)

| Canal | Porte quoi | Règle |
|---|---|---|
| **Remplissage de cellule** | le CONTENU | vert `kCellOn`/`kCellOff` ; teinte harmonique `pitchClassColour` si la row est liée |
| **Luminosité du remplissage** | l'INTENSITÉ (vélocité) | `velA = 0.45 + 0.55·vel/127` |
| **Liseré (contour)** | état transitoire / focus | épaisseur = priorité : sélection (blanc, gros) > clipboard (bleu) > playhead/grille |
| **Coin haut-gauche** | marqueur SNAPPÉ (note tirée hors gamme) | petit triangle ambre |
| **Coin haut-droit** | mode sub REL/ABS | badge vert (REL) / ambre (ABS) |
| **Coin BAS-GAUCHE** | **STATUT du sous-pattern** | triangle plein : **magenta** = indépendant, **cyan** = partagé/aliasé (ghost) |
| **Atténuation (alpha)** | inactif / muted | α réduit (0.30) |

## Couleurs (rôles)

| Couleur | Constante | Rôle |
|---|---|---|
| Vert clair | `kCellOn` | pas/sous-pas actif |
| Vert sombre | `kCellOff` | pas/sous-pas inactif, fond de cellule à sub |
| Blanc | `kSelStep` | **sélection** (curseur) — liseré épais, gagne TOUJOURS (dessiné en dernier) |
| Bleu vif / sombre | `kClipCut` / `kClipCopy` | **presse-papier** coupé / copié — liseré bleu |
| Ambre | `kPlayhead` | **« lecture/temps » au sens large** — voir note ci-dessous |
| Teintes HSV | `pitchClassColour(pc)` | **zone harmonique** : teinte = note jouée après filtre |
| Cyan | `kGhost` | sous-pattern **partagé** (ghost/alias) — triangle plein, coin bas-gauche |
| Magenta | `kSubSolo` | sous-pattern **indépendant** (propre à un seul pas) — triangle plein, coin bas-gauche |
| Rougeâtre | `kMutedText` | labels d'une row mutée |

### Note sur l'ambre (acté : distinction par FORME, pas par couleur)
L'ambre `kPlayhead` porte volontairement **4 usages** ; ils ne se confondent pas
car chacun a une **forme/position distincte** :
- **playhead** = liseré fin qui *bouge* autour de la cellule jouée ;
- **badge ABS** = pastille fixe au coin *haut-droit* ;
- **marqueur snappé** = triangle au coin *haut-gauche* ;
- **indicateur de page** = voile très translucide (α 0.12) sur une fenêtre de 16 pas.
→ Pas de changement de code : l'ambre = « repère temporel/lecture », désambiguïsé
par la forme. Ne PAS ajouter d'autre usage *liseré* en ambre.

## Cumul d'états — ordre de dessin (z-order), du dessous au dessus
1. Remplissage (contenu + teinte harmonique + luminosité vélo)
2. Liseré de contour (playhead ambre, sinon grille)
3. Contenu du sub (mini-blocs) / texte de note résolue
4. Coin haut-gauche : triangle snappé
5. Coin haut-droit : badge REL/ABS
6. **Coin bas-gauche : marqueur ghost (cyan)**
7. Liseré clipboard (bleu)
8. Liseré sélection (blanc) — **toujours au-dessus**

Conséquence : un pas peut être simultanément *sélectionné + en lecture + copié +
harmonisé + sub partagé* et rester lisible — chaque info occupe son canal/coin.

## Règles pour toute évolution
- **Un nouvel état → un canal/coin libre**, pas une couleur de plus sur un canal occupé.
- Couleurs réservées : blanc=sélection, bleu=clipboard, ambre=temps, vert=contenu,
  cyan=partage. Pour un futur état, privilégier : coin bas-droit (libre), magenta/rose,
  ou un motif (hachures).
- `log()`/documenter ici tout nouvel état ajouté.

## États couverts aujourd'hui (récap)
off/on, vélocité, sélection, playhead, presse-papier copié/coupé, sous-pattern
(mini-blocs), pas-hôte désactivé (α), span étendu + pas recouverts (masqués),
zone harmonique (teinte), snappé, mode REL/ABS, row mutée, **statut de sous-pattern
(triangle coin bas-gauche : magenta = indépendant, cyan = partagé/ghost)** en
PATTERN + ROLL. Détacher un partagé : **⇧+Mode (noire 9)** → copie indépendante
(le triangle repasse magenta).
