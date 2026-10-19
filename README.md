# RavenEmuDSK

Émulateur PC open source **Game Boy, Game Boy Color et Game Boy Advance**, en **C++20**, pour **Windows, Linux et macOS**.

- **RavenCore** : moteurs C++ natifs GB/GBC/GBA, API publique C.
- **Interface** : adaptation de l'interface C++ SDL2 / Dear ImGui du projet [Retrophora-Emulateur](https://github.com/Retrophora/Retrophora-Emulateur), avec le thème de RavenEmu (`#0B0C12`, `#191B28`, `#A78BFA`).
- **RetroAchievements** : intégration officielle `rcheevos`, identification des ROM et lecture mémoire de RavenCore, authentification HTTPS, notifications et état du compte. Pas d'Engine/SDK Retrophora ni de règles de succès JSON locaux.
- **Logo** : asset officiel de RavenEmu copié au build depuis une révision immuable de [RavenEmu](https://github.com/GhostPunishR/RavenEmu). Pour un build hors ligne, préremplir `build/branding/ravenemu-logo.png` avec le logo fourni.

## Compilation

Nécessite CMake 3.22+, Git, un compilateur C++20, SDL2 et libcurl de développement. CMake récupère des révisions épinglées de RavenCore, Dear ImGui, stb et du SDK rcheevos. Première configuration : connexion Internet requise.

### Linux (Ubuntu / Debian)
```sh
sudo apt install cmake g++ git libsdl2-dev libcurl4-openssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/RavenEmuDSK
```

### macOS (Homebrew)
```sh
brew install cmake sdl2 curl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/RavenEmuDSK
```

### Windows (Visual Studio / vcpkg)
```powershell
vcpkg install sdl2:x64-windows curl:x64-windows
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
.\build\Release\RavenEmuDSK.exe
```

Les builds et tests des trois systèmes sont automatisés dans **Actions**. Les artefacts ne sont pas des releases officiellement validées.

## Fonctions

La bibliothèque, les jaquettes locales, les imports, le mode TV/manette et les commandes proviennent de Retrophora et ont été rebrandés pour RavenEmuDSK. En jeu : écran SDL, audio, clavier/manette, sauvegardes batterie, états rapides et notifications. `Échap` affiche le menu ; `Z/X` = A/B ; `F5/F9` = sauvegarder/charger l'état (désactivés en Hardcore).

Ouvrez **Succès** ou **Profil** pour vous connecter à RetroAchievements. Le mot de passe n'est pas sauvegardé. Les succès sont identifiés pour les ROM reconnues et évalués par le SDK officiel ; aucun succès local inventé n'est soumis.

> **État : premier portage**. La compilation, les plateformes, le comportement réseau et la conformité du mode Hardcore restent à valider en CI et sur de vrais appareils. Le mode Hardcore n'est pas annoncé conforme avant contrôle de toutes les règles de RetroAchievements. Pas de ROM commerciales incluses.

## Crédits et licence

© 2026 GhostPunishR. Code d'interface adapté de Retrophora-Emulateur (même auteur), moteurs [RavenCore](https://github.com/GhostPunishR/RavenCore). Les bibliothèques tierces conservent leurs licences. Logiciel sous **AGPL-3.0-only**. Aucun lien officiel avec Nintendo ou RetroAchievements.
