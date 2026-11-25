# Swell Essential Guitar

Instrument sampler (JUCE 8) : 52 presets de guitare répartis en 6 catégories
(Acoustic, Bass, Electric, Muted, Nylon, Rock). Formats : VST3, AU, Standalone.

## Samples

Le plugin lit les WAV directement dans le dossier de la banque, par défaut
`/Volumes/HDD/Swell eseential Guitar Sample`. Structure attendue :

```
<dossier>/<Catégorie>/<Preset>/<Preset>_<Note><Octave>_<Vélocité>.wav
```

Les octaves suivent FL Studio / DirectWave : **C5 = note MIDI 60**. La plage
enregistrée va de C3 à B7. Les notes hors plage sont transposées depuis le sample
le plus proche (jusqu'à une octave). Les fichiers `.dwp` sont ignorés.
Si le disque change de nom, l'icône dossier en haut à droite permet de choisir
un autre emplacement. Le chemin et le preset sont enregistrés dans le projet de l'hôte.

## Interface et son

- Flèches ‹ › : preset suivant/précédent ; elles passent d'une catégorie à l'autre aux extrémités.
- Clic sur le nom du preset : menu Favoris / catégories / presets. L'étoile (menu ou sous le titre)
  ajoute aux favoris, enregistrés dans `~/Library/Application Support/Swell Audio/Essential Guitar/favourites.txt`
  et partagés par toutes les instances.
- Changement de preset sans coupure : les notes en cours continuent sur l'ancien son, le nouveau
  se charge en arrière-plan (fine barre sous le nom). Les presets voisins sont préchargés (5 en mémoire).
- Toutes les notes MIDI jouent : celles sans sample sont transposées depuis le sample le plus proche.
- Bouton SLIDE (notes liées) : une note jouée pendant qu'une autre est tenue glisse depuis celle-ci.
- Volume homogène : chaque preset est ramené au même niveau perçu (attaque des notes, pondération proche LUFS),
  puis un limiteur de sécurité à -0,3 dBFS.
- SON : tone, attack, release, EQ (creux doux à 320 Hz + un peu d'air).
- EFFETS : chorus (largeur), delay ping-pong au tempo, reverb stéréo (FDN 8 lignes, sans bas-médiums).
- SORTIE : volume.

Samples exclus car faux (la note est transposée depuis ses voisines) et presets masqués :
listes `badSamples` et `hiddenPresets` en tête de `Source/Library.cpp`.

## Compiler

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target SwellEssentialGuitar_VST3 SwellEssentialGuitar_AU SwellEssentialGuitar_Standalone -j 8
```

JUCE est pris dans `../Swell-Reverse/JUCE` (modifiable avec `-DJUCE_DIR=`).

## Test hors ligne

```sh
cmake -S . -B build -DEG_TESTS=ON && cmake --build build --target EGCheck
./build/EGCheck "/Volumes/HDD/Swell eseential Guitar Sample" captures
```

Vérifie la lecture des noms de fichiers, la banque (52 presets), le son, la
navigation entre catégories, la sauvegarde d'état, et enregistre une capture
PNG de l'interface pour chaque catégorie.
