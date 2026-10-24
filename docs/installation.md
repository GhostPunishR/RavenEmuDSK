# Installation et compilation

[Retour au README](../README.md)

## Télécharger et lancer

Les [artefacts GitHub Actions](https://github.com/GhostPunishR/RavenEmuDSK/actions) contiennent les paquets de chaque compilation réussie. Aucune ROM n'est fournie.

- **Windows x64** : extrayez l'archive et lancez `RavenEmuDSK.exe`, en conservant les DLL et le dossier `assets` à côté.
- **Linux x86_64** : extrayez `RavenEmu-Linux-x86_64.tar.gz`, puis lancez `RavenEmu/RavenEmuDSK`. Les bibliothèques SDL2, libcurl, libsecret et leurs dépendances sont incluses dans `lib`, sans installation par apt. Conservez l'ensemble du dossier. La compilation CI nécessite glibc 2.39 ou plus récente, par exemple Ubuntu 24.04+ ou Debian 13.
- **macOS 15+** : extrayez l'archive `RavenEmu-macOS-<architecture>.zip` et déplacez `RavenEmu.app` dans Applications. SDL2 et ses dépendances, dont SDL3 lorsque Homebrew utilise `sdl2-compat`, sont intégrées à l'application ; libcurl et les frameworks Apple sont fournis par macOS. Homebrew n'est pas nécessaire pour utiliser ce paquet. L'architecture du Mac de compilation est indiquée dans le nom de l'archive. L'application possède une signature locale de validation, sans notarisation Apple ; macOS peut demander de l'autoriser dans Confidentialité et sécurité.

Sous Linux, le bureau fournit les pilotes graphiques, les services audio et le magasin de certificats HTTPS. La mémorisation de la session RetroAchievements nécessite un trousseau Secret Service disponible et déverrouillé, par exemple GNOME Keyring ou KWallet compatible. Les bibliothèques fournies ne créent pas ce service à la place de votre session de bureau.

Les paquets Unix contiennent les licences de leurs bibliothèques et un fichier `dependencies.json` avec leurs versions et la provenance de leurs sources.

## Compiler depuis les sources

### Prérequis

- CMake 3.22 ou plus récent, Git et un compilateur compatible C++20.
- Les bibliothèques de développement SDL2 et libcurl.
- Python 3 pour les contrôles du dépôt et la création des paquets Unix.
- Une connexion Internet pour récupérer les dépendances à la première configuration.

RavenCore, Dear ImGui, stb et rcheevos sont récupérés par CMake à des révisions fixées dans le dépôt. Le logo RavenEmu sans marge grise est inclus dans `assets/ravenemu-logo.png`, à partir de l’[icône originale du projet RavenEmu](https://github.com/GhostPunishR/RavenEmu/blob/5f002726a68c1978f4cad273bf8b7b3786b9fb3b/docs/assets/ravenemu-icon.png). Il est utilisé dans l’interface et pour les icônes des fenêtres.

La révision RavenCore intégrée est [`15675cc`](https://github.com/GhostPunishR/RavenCore/commit/15675cc3af4f9b07f2e10cc5a7ca6234ea1c77cc), celle de la version 0.14.0. Elle prend en charge le MBC30 de *Pocket Monsters Crystal Version* (0.13.0) et passe les états GBA au format 17 : les anciens se rechargent, mais un état GBA pris avec elle ne se recharge pas dans une version antérieure de RavenEmuDSK. Son API C gagne les codes de triche, la GBC d'une Game Boy Advance, les couleurs des jeux Game Boy sur GBC et un vrai BIOS GBA facultatif, que l'application n'utilise pas encore. Elle garde le câble link distant, qui relie la console à celle d'un autre appareil, dans sa version 2 qui rend possibles les échanges Pokémon, même sur un Wi-Fi lent (0.11.0), et garde la carte mémoire RetroAchievements Game Boy et Game Boy Color complète de la 0.8.0 ainsi que les corrections RTC GBA. Le moteur est compilé dans l'application : aucun téléchargement séparé de RavenCore n'est nécessaire pour jouer.

Récupérez les sources :

```sh
git clone https://github.com/GhostPunishR/RavenEmuDSK.git
cd RavenEmuDSK
```

### Linux (Debian / Ubuntu)

```sh
sudo apt-get update
sudo apt-get install -y cmake g++ git python3 pkg-config libsdl2-dev libcurl4-openssl-dev libsecret-1-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/RavenEmuDSK
```

### macOS

Installez les outils de compilation Xcode et les dépendances avec Homebrew :

```sh
xcode-select --install
brew install cmake sdl2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  "-DCURL_LIBRARY=$(xcrun --show-sdk-path)/usr/lib/libcurl.tbd" \
  "-DCURL_INCLUDE_DIR=$(xcrun --show-sdk-path)/usr/include"
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
