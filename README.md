# RavenEmu

**La version PC de RavenEmu pour Game Boy, Game Boy Color et Game Boy Advance.**

RavenEmu est un émulateur open source écrit en C++20, avec une interface SDL2 / Dear ImGui et les moteurs d'émulation [RavenCore](https://github.com/GhostPunishR/RavenCore). Ce dépôt contient l'application de bureau destinée à Windows, Linux et macOS.

Le projet est en développement. La compatibilité des jeux et le comportement sur chaque plateforme peuvent encore évoluer.

## Fonctionnalités

- Bibliothèque de jeux avec import de fichiers ou de dossiers, glisser-déposer et jeux récents.
- Jaquettes locales et mode TV avec navigation au clavier ou à la manette.
- Audio, commandes clavier/manette et plein écran pendant le jeu.
- Sauvegardes de progression et sauvegarde/chargement rapide d'un état.
- Profil RetroAchievements, consultation des succès et notifications en jeu.
- Vérification des nouvelles versions depuis l'application.

## Compiler

### Prérequis

- CMake 3.22 ou plus récent, Git et un compilateur compatible C++20.
- Les bibliothèques de développement SDL2 et libcurl.
- Une connexion Internet pour récupérer les dépendances à la première configuration.

RavenCore, Dear ImGui, stb et rcheevos sont récupérés par CMake à des révisions fixées dans le dépôt. Le logo RavenEmu sans marge grise est inclus dans `assets/ravenemu-logo.png`, à partir de l’[icône originale du projet RavenEmu](https://github.com/GhostPunishR/RavenEmu/blob/5f002726a68c1978f4cad273bf8b7b3786b9fb3b/docs/assets/ravenemu-icon.png). Il est utilisé dans l’interface et pour les icônes des fenêtres.

Récupérez les sources :

```sh
git clone https://github.com/GhostPunishR/RavenEmuDSK.git
cd RavenEmuDSK
```

### Linux (Debian / Ubuntu)

```sh
sudo apt-get update
sudo apt-get install -y cmake g++ git pkg-config libsdl2-dev libcurl4-openssl-dev libsecret-1-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/RavenEmuDSK
```

### macOS

Installez les outils de compilation Xcode et les dépendances avec Homebrew :

```sh
xcode-select --install
brew install cmake sdl2 curl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/RavenEmuDSK
```

### Windows

Utilisez Visual Studio 2022 avec les outils de développement C++, CMake et [vcpkg](https://github.com/microsoft/vcpkg). La variable `VCPKG_ROOT` doit désigner votre installation de vcpkg. Depuis PowerShell, dans le dossier du dépôt :

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install sdl2:x64-windows curl:x64-windows
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel 2
.\build\Release\RavenEmuDSK.exe
```

L'application s'affiche sous le nom **RavenEmu**. Le dépôt et le fichier exécutable conservent le nom technique `RavenEmuDSK`.

## Utilisation

1. Lancez l'application, puis ajoutez vos fichiers `.gb`, `.gbc` ou `.gba` à la bibliothèque. Vous pouvez aussi déposer un fichier ou un dossier dans la fenêtre.
2. Sélectionnez un jeu pour le lancer. Les ROM restent à leur emplacement d'origine.
3. Ouvrez **Réglages** pour consulter les dossiers de sauvegardes et de jaquettes, les commandes ou activer le mode TV.
4. Ouvrez **Profil** pour vous connecter à RetroAchievements si vous souhaitez utiliser les succès.

Aucune ROM commerciale n'est fournie. L'émulation ne nécessite pas de compte RetroAchievements.

### Commandes clavier en jeu

| Action | Touche |
| --- | --- |
| Déplacement | Flèches |
| Boutons A / B | Z / X |
| Start / Select | Entrée / Retour arrière |
| Gâchettes L / R (GBA) | A / S |
| Menu de pause | Échap |
| Pause rapide | P |
| Sauvegarder un état | F5 |
| Charger un état | F9 |
| Réinitialiser le jeu | F4 |
| Plein écran | F11 |

Les commandes à la manette sont indiquées dans **Réglages**.

### Sauvegardes et jaquettes

L'application utilise les dossiers `Sauvegardes` et `Jaquettes` à côté de l'exécutable. Si cet emplacement n'est pas accessible en écriture, elle essaie son dossier de données utilisateur. Les chemins utilisés sont affichés dans **Réglages**.

La bibliothèque et les réglages sont enregistrés séparément dans le dossier de données utilisateur. Copier une ROM ne transfère donc pas automatiquement sa progression : conservez également vos sauvegardes.

Les sauvegardes sont isolées dans un sous-dossier par chemin de ROM afin que deux jeux portant le même nom ne puissent pas écraser leur progression. Les anciens fichiers sont copiés vers ce classement et conservés à leur emplacement d'origine. Un ancien fichier partagé entre plusieurs ROM de même nom est attribué au premier jeu qui le migre ; vérifiez cette progression si vous aviez déjà des doublons. Si vous déplacez une ROM, conservez son ancien dossier de sauvegarde pour transférer sa progression.

### RetroAchievements

L'intégration repose sur le SDK [rcheevos](https://github.com/RetroAchievements/rcheevos). Depuis **Profil**, vous pouvez vous connecter, consulter vos points et les succès du jeu reconnu. En jeu, les déblocages et la progression des succès sont affichés sous forme de notifications.

- Une connexion Internet et une ROM reconnue par RetroAchievements sont nécessaires pour les fonctions en ligne.
- Le mode proposé est **Softcore**. Le mode Hardcore n'est pas proposé dans cette version.
- Le mot de passe n'est jamais sauvegardé. Sous Windows, le jeton de session est conservé dans le Gestionnaire d'identifiants de votre compte Windows pour vous reconnecter automatiquement au démarrage. **Déconnexion** ou **Oublier la session** efface cette connexion mémorisée.
- Sous macOS, le jeton est conservé dans le Trousseau. Sous Linux, la mémorisation utilise Secret Service via `libsecret` et nécessite un trousseau disponible et déverrouillé (GNOME Keyring, par exemple). Sans coffre disponible, la connexion reste utilisable pour la session en cours.
- En cas de panne réseau, la reconnexion par jeton et la recherche du jeu sont réessayées après 5, 10, 20, 40 puis 60 secondes au maximum. **Réessayer** permet une tentative immédiate. Un jeton expiré demande une nouvelle connexion.
- Les messages distinguent une ROM inconnue, un jeu reconnu sans succès et un serveur indisponible. Les envois de succès en attente restent gérés par le SDK pendant l'exécution ; ils ne sont pas conservés après fermeture de l'application.
- Le profil public et les succès des ROMs déjà reconnues sont mémorisés par compte et indiqués comme données mémorisées. Le panneau de droite, le résumé d'accueil et le mode TV affichent les compteurs du jeu sélectionné ; **Voir tous les succès** ouvre sa liste dans Profil. Ces données ne permettent ni de se connecter ni de débloquer un succès hors connexion. **Déconnexion** efface aussi ce cache.
- Les badges verrouillés et débloqués utilisent leurs images respectives. Les téléchargements échoués sont réessayés et le cache évince les anciennes images. Les notifications de succès s'affichent successivement ; leur durée est suspendue pendant le menu de pause.

La [validation RetroAchievements](docs/retroachievements-validation.md) précise les tests automatiques, les vérifications manuelles restantes et les prérequis avant toute ouverture du mode Hardcore.

## Développement et tests

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

Le [workflow de compilation](.github/workflows/build.yml) prévoit des builds et tests sur Ubuntu, Windows et macOS. Les résultats et artefacts sont consultables dans [GitHub Actions](https://github.com/GhostPunishR/RavenEmuDSK/actions).

## Licence et crédits

© 2026 GhostPunishR. RavenEmu est distribué sous licence [AGPL-3.0-only](LICENSE).

Le projet utilise RavenCore, SDL2, Dear ImGui, stb_image, libcurl et rcheevos. Ces dépendances conservent leurs licences respectives ; consultez [NOTICE.md](NOTICE.md) et leurs dépôts pour les détails.

RavenEmu est un projet indépendant, sans affiliation officielle avec Nintendo ou RetroAchievements.
