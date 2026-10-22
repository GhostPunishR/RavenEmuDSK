# Développer RavenEmu

[Guide de contribution](../CONTRIBUTING.md) · [Installation et compilation](installation.md)

## Organisation du code

| Emplacement | Rôle |
| --- | --- |
| `src/desktop.cpp` | Point d'entrée, boucle SDL et intégration de l'application. |
| `src/ui/` | Bibliothèque, interface ImGui, réglages, commandes et stockage local. |
| `src/achievements/` | Client RetroAchievements, session, cache et journal des envois. |
| `include/ravenemudsk/` | Interfaces C++ de ces modules. |
| `tests/` | Tests CTest et données synthétiques. |
| `assets/` | Logo, icônes et ressources de l'interface. |
| `cmake/` | Intégration des dépendances et assemblage macOS. |
| `tools/` | Vérifications Python et création des paquets Unix. |
| `.github/workflows/build.yml` | Compilation, tests et paquets Windows, Linux et macOS. |

L'application porte le nom **RavenEmu**. `RavenEmuDSK` reste le nom du dépôt, de la cible CMake et de l'exécutable. Les moteurs GB/GBC/GBA sont maintenus dans [RavenCore](https://github.com/GhostPunishR/RavenCore).

## Dépendances et ressources

[CMakeLists.txt](../CMakeLists.txt) fixe les révisions de RavenCore, Dear ImGui, stb et rcheevos. Pour mettre à jour une dépendance, expliquez le changement, vérifiez sa licence et testez les fonctions concernées sur les plateformes touchées.

Deux correctifs rcheevos sont appliqués à une copie de ses sources par [cmake/rcheevos_safety.cmake](../cmake/rcheevos_safety.cmake). Réexaminez ces correctifs lors d'une mise à jour du SDK. Ne modifiez pas les sources téléchargées dans `build/_deps` : ces modifications disparaissent à la prochaine configuration.

Le logo de référence est [assets/ravenemu-logo.png](../assets/ravenemu-logo.png). Conservez son aspect et sa transparence dans les vues et les icônes ; n'ajoutez pas de cadre ou de marge grise. Les autres mentions de licence sont dans [NOTICE.md](../NOTICE.md).

## Tests et vérifications

Les tests CTest sont activés par défaut. Après compilation :

```sh
ctest --test-dir build --output-on-failure
```

Avec Visual Studio, ajoutez `-C Release` pour tester la configuration compilée. Les vérifications des tests restent actives en Release. Pour déboguer l'application, utilisez une compilation **Debug**.

Deux vérifications Python complètent les tests :

```sh
python3 tools/check_text_style.py
python3 tools/check_ra_navigation.py
```

Sous Windows, utilisez `python` si c'est le nom de votre commande Python. Les textes du dépôt utilisent le tiret simple `-`, conformément au contrôle de typographie.

Le [workflow de compilation](../.github/workflows/build.yml) prévoit des builds et tests sur Ubuntu, Windows et macOS. Les résultats et artefacts sont consultables dans [GitHub Actions](https://github.com/GhostPunishR/RavenEmuDSK/actions).

## Création des paquets

Pour générer les mêmes paquets Unix après compilation :

```sh
python3 tools/package_unix.py --build build --output dist
```

L'outil nécessite `patchelf` et les métadonnées des paquets Debian/Ubuntu sous Linux ; sous macOS, il utilise CMake, les outils Xcode et les licences des formules Homebrew de compilation. Il rassemble les bibliothèques, corrige leurs chemins et vérifie les pages principales depuis un emplacement contenant des espaces. La CI Linux relance aussi le paquet dans une image Ubuntu minimale, sans installation des bibliothèques de l'application. Les services graphiques et audio réels restent à vérifier sur un poste de bureau.

## Validation manuelle

Pour une modification de l'interface, vérifiez le parcours concerné en petite et grande fenêtre, ainsi qu'au clavier et à la manette lorsque ces entrées s'appliquent. Joignez une capture si elle aide à comprendre le changement.

Pour RetroAchievements, suivez la [matrice de validation](retroachievements-validation.md) et la [politique de confidentialité](confidentialite.md). Les tests automatiques utilisent des ROMs synthétiques et des transports simulés ; ils n'accordent pas de succès sur un vrai compte. Ne présentez pas un résultat CTest comme une approbation officielle RetroAchievements.
