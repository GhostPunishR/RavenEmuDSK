# RavenEmuDSK

Émulateur PC open source **Game Boy, Game Boy Color et Game Boy Advance**, en **C++20**, pour **Windows, Linux et macOS**.

- **RavenCore** : moteurs C++ natifs GB/GBC/GBA, API publique C.
- **Interface** : interface C++ SDL2 / Dear ImGui avec le thème de RavenEmu (`#0B0C12`, `#191B28`, `#A78BFA`).
- **RetroAchievements** : intégration officielle `rcheevos`, identification des ROM et lecture mémoire de RavenCore, authentification HTTPS, notifications et état du compte. Pas de moteur de succès local ni de règles JSON de succès.
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

RavenEmuDSK propose une bibliothèque de jeux, des jaquettes locales, des imports, un mode TV/manette et des commandes clavier/manette. En jeu : écran SDL, audio, clavier/manette, sauvegardes batterie, états rapides et notifications. `Échap` affiche le menu ; `Z/X` = A/B ; `F5/F9` = sauvegarder/charger l'état (désactivés en Hardcore).

Ouvrez **Succès** ou **Profil** pour vous connecter à RetroAchievements. Le mot de passe n'est pas sauvegardé. Les succès sont identifiés pour les ROM reconnues et évalués par le SDK officiel ; aucun succès local inventé n'est soumis. **Seul le mode Softcore est proposé à ce stade**, le mode Hardcore nécessitant une vérification de conformité.

> **État : premier portage**. La compilation, les plateformes, le comportement réseau et la conformité du mode Hardcore restent à valider en CI et sur de vrais appareils. Le mode Hardcore n'est pas annoncé conforme avant contrôle de toutes les règles de RetroAchievements. Pas de ROM commerciales incluses.

## Crédits et licence

© 2026 GhostPunishR. Interface et intégration C++ par GhostPunishR, moteurs [RavenCore](https://github.com/GhostPunishR/RavenCore). Les bibliothèques tierces conservent leurs licences. Logiciel sous **AGPL-3.0-only**. Aucun lien officiel avec Nintendo ou RetroAchievements.

## Convention typographique

Utiliser uniquement le tiret simple `-` dans les textes du depot. Les tirets typographiques longs sont interdits. Cette regle est verifiee par la CI.

## Popups RetroAchievements

Les notifications en jeu reproduisent les composants RavenEmu :

- Succès débloqué en haut au centre : badge officiel de 40 px, fond sombre, bordure violette et texte blanc.
- Progression mesurée dans une seconde carte sous le succès : valeur fournie directement par le SDK.
- Fondu de 300 ms, durée de 4,8 s pour chaque succès et de 2 s pour chaque progression.
- Déblocages simultanés en file d'attente, sans remplacement prématuré.
- Défis actifs et indicateurs des classements en bas jusqu'à l'événement de fin.
- Images récupérées uniquement sur les hôtes HTTPS de RetroAchievements, avec une étoile violette si le badge n'est pas disponible.

Le premier relevé des valeurs de progression sert de base : il ne déclenche pas une fausse notification.

## Profil RetroAchievements

La page Profil est l'unique tableau de bord RetroAchievements. L'entrée Succès n'apparaît plus dans la barre latérale. Elle affiche le statut de connexion, l'avatar du compte, le pseudo, les points Softcore et Hardcore, puis les informations du dernier jeu reconnu. Les boutons Réessayer et Déconnexion fonctionnent pendant la session.

Les succès du jeu peuvent être filtrés par Tous, À obtenir et Obtenus, consultés par groupes de 10 puis 15, avec badge officiel et fiche détaillée. Les liens des succès ouvrent RetroAchievements dans le navigateur.

Sur l'accueil et dans la barre latérale, l'état déconnecté dessine le logo RA en bleu et or directement avec ImDrawList, d'après le monogramme de référence. Dès la connexion, la même zone montre l'avatar officiel du compte chargé en HTTPS, avec les initiales comme solution provisoire si l'image n'est pas encore disponible. Le jeton de session ne persiste pas sur disque dans cette version.
