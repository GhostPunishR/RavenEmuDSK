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

## Télécharger et lancer

Les [artefacts GitHub Actions](https://github.com/GhostPunishR/RavenEmuDSK/actions) contiennent les paquets de chaque compilation réussie. Aucune ROM n'est fournie.

- **Windows x64** : extrayez l'archive et lancez `RavenEmuDSK.exe`, en conservant les DLL et le dossier `assets` à côté.
- **Linux x86_64** : extrayez `RavenEmu-Linux-x86_64.tar.gz`, puis lancez `RavenEmu/RavenEmuDSK`. Les bibliothèques SDL2, libcurl, libsecret et leurs dépendances sont incluses dans `lib`, sans installation par apt. Conservez l'ensemble du dossier. La compilation CI nécessite glibc 2.39 ou plus récente, par exemple Ubuntu 24.04+ ou Debian 13.
- **macOS 15+** : extrayez l'archive `RavenEmu-macOS-<architecture>.zip` et déplacez `RavenEmu.app` dans Applications. SDL2 et ses dépendances, dont SDL3 lorsque Homebrew utilise `sdl2-compat`, sont intégrées à l'application ; libcurl et les frameworks Apple sont fournis par macOS. Homebrew n'est pas nécessaire pour utiliser ce paquet. L'architecture du Mac de compilation est indiquée dans le nom de l'archive. L'application possède une signature locale de validation, sans notarisation Apple ; macOS peut demander de l'autoriser dans Confidentialité et sécurité.

Sous Linux, le bureau fournit les pilotes graphiques, les services audio et le magasin de certificats HTTPS. La mémorisation de la session RetroAchievements nécessite un trousseau Secret Service disponible et déverrouillé, par exemple GNOME Keyring ou KWallet compatible. Les bibliothèques fournies ne créent pas ce service à la place de votre session de bureau.

Les paquets Unix contiennent les licences de leurs bibliothèques et un fichier `dependencies.json` avec leurs versions et la provenance de leurs sources.

## Compiler

### Prérequis

- CMake 3.22 ou plus récent, Git et un compilateur compatible C++20.
- Les bibliothèques de développement SDL2 et libcurl.
- Une connexion Internet pour récupérer les dépendances à la première configuration.

RavenCore, Dear ImGui, stb et rcheevos sont récupérés par CMake à des révisions fixées dans le dépôt. Le logo RavenEmu sans marge grise est inclus dans `assets/ravenemu-logo.png`, à partir de l’[icône originale du projet RavenEmu](https://github.com/GhostPunishR/RavenEmu/blob/5f002726a68c1978f4cad273bf8b7b3786b9fb3b/docs/assets/ravenemu-icon.png). Il est utilisé dans l’interface et pour les icônes des fenêtres.

La révision RavenCore intégrée est [`241f56c`](https://github.com/GhostPunishR/RavenCore/commit/241f56c3564ca1389813753d5c13dd931771fe40), qui inclut les corrections RTC GBA et la détection des pilotes Pokémon compatibles sans marqueur Seiko. Le moteur est compilé dans l'application : aucun téléchargement séparé de RavenCore n'est nécessaire pour jouer.

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

Ces touches sont les valeurs par défaut. Dans **Réglages > Commandes**, cliquez sur une touche puis appuyez sur celle souhaitée : boutons de la console et raccourcis d'émulation sont personnalisables et mémorisés pour les prochains lancements. Les touches déjà utilisées sont échangées. **Non attribuée** libère une commande et **Rétablir les touches par défaut** restaure l'ensemble. Échap annule une modification et reste toujours disponible pour ouvrir ou fermer le menu en jeu. Les indications du menu de pause suivent votre configuration. Les commandes à la manette sont également indiquées dans cette catégorie.

### Sauvegardes et jaquettes

Sous Windows et Linux, l'application utilise les dossiers `Sauvegardes` et `Jaquettes` à côté de l'exécutable. Si cet emplacement n'est pas accessible en écriture, elle essaie son dossier de données utilisateur. L'application `.app` macOS place ces dossiers dans les données utilisateur pour préserver sa signature ; une compilation macOS en exécutable simple garde le comportement portable. Les chemins utilisés sont affichés dans **Réglages**.

La bibliothèque et les réglages sont enregistrés séparément dans le dossier de données utilisateur. Copier une ROM ne transfère donc pas automatiquement sa progression : conservez également vos sauvegardes.

Les sauvegardes sont isolées dans un sous-dossier par chemin de ROM afin que deux jeux portant le même nom ne puissent pas écraser leur progression. Les anciens fichiers sont copiés vers ce classement et conservés à leur emplacement d'origine. Un ancien fichier partagé entre plusieurs ROM de même nom est attribué au premier jeu qui le migre ; vérifiez cette progression si vous aviez déjà des doublons. Si vous déplacez une ROM, conservez son ancien dossier de sauvegarde pour transférer sa progression.

### RetroAchievements

L'intégration repose sur le SDK [rcheevos](https://github.com/RetroAchievements/rcheevos). **Profil** et le menu de pause donnent accès aux succès, aux classements et à l'activité du jeu. **Actualiser les succès**, dans une fiche, récupère les données du compte et du jeu sans lancer l'émulation. Les déblocages, défis et progressions mesurées s'affichent pendant la partie.

- Une connexion Internet et une ROM reconnue par RetroAchievements sont nécessaires pour les fonctions en ligne.
- **Softcore** est sélectionné au départ. **Mode Hardcore**, dans Profil ou Réglages > RetroAchievements, mémorise votre choix. Le menu de pause permet aussi de changer de mode. Passer en Hardcore réinitialise entièrement la console et bloque tous les chargements d'états, y compris pendant la connexion. Revenir en Softcore conserve la partie. Sauvegarder un état reste possible en Hardcore pour le débogage.
- Le Hardcore nécessite un compte connecté, une ROM reconnue et un coffre système disponible pour protéger les envois hors ligne. En attendant la connexion ou la reconnaissance, l'émulation attend ; le menu permet de revenir en Softcore. Une panne du journal protégé interrompt le Hardcore et indique le passage en Softcore. **Aucune approbation officielle Hardcore par RetroAchievements n'est revendiquée.**
- Le mot de passe n'est jamais sauvegardé. Sous Windows, le jeton de session est conservé dans le Gestionnaire d'identifiants de votre compte Windows pour vous reconnecter automatiquement au démarrage. **Déconnexion** ou **Oublier la session** efface cette connexion mémorisée.
- Sous macOS, le jeton est conservé dans le Trousseau. Sous Linux, la mémorisation utilise Secret Service via `libsecret` et nécessite un trousseau disponible et déverrouillé (GNOME Keyring, par exemple). Sans coffre disponible, la connexion reste utilisable pour la session en cours.
- En cas de panne réseau, la reconnexion par jeton et la recherche du jeu sont réessayées après 5, 10, 20, 40 puis 60 secondes au maximum. **Réessayer** permet une tentative immédiate. Un jeton expiré demande une nouvelle connexion. Un refus définitif du service au chargement d'un jeu (session ou accès refusé) n'est pas présenté comme une panne et n'est pas réessayé en boucle : une session refusée est renouvelée une fois, puis une nouvelle connexion est demandée.
- Les messages distinguent une ROM inconnue, un jeu reconnu sans succès et un serveur indisponible. Avec un coffre disponible, les succès et scores en attente sont conservés après fermeture dans un journal authentifié, puis repris avec le jeton actuel. Le profil indique le nombre d'envois en attente. Ils sont repris avant la recherche du jeu ; un envoi que le service n'accepte pas reste en attente sans empêcher le chargement des succès. Sans coffre, les reprises sont limitées à l'exécution en cours : gardez l'application ouverte jusqu'à la synchronisation.
- Les états Softcore conservent ensemble l'état de la console et les compteurs de conditions des succès, liés au compte et à l'empreinte de la ROM. Les anciens états restent lisibles ; leurs compteurs RetroAchievements sont réinitialisés.
- Les succès déjà obtenus sur votre compte sont récupérés avant le suivi de la partie, y compris ceux gagnés sur un autre émulateur. Si la vérification initiale échoue, le statut indique la panne et la synchronisation est réessayée ; une panne ne doit pas être présentée comme un historique vide. **Actualiser les succès** permet aussi de remettre à jour une ancienne fiche mémorisée.
- Le profil public et les succès des ROMs déjà reconnues sont mémorisés par compte et indiqués comme données mémorisées. Le panneau de droite, le résumé d'accueil et le mode TV affichent les compteurs du jeu sélectionné ; **Voir tous les succès** ouvre sa liste dans Profil. Ces données seules ne permettent ni de se connecter ni de débloquer un succès. **Déconnexion**, dans l'en-tête du compte, demande une confirmation qui indique le nombre d'envois encore en attente, puis efface le jeton, le cache, ces envois et leur clé de protection.
- **Profil** garde en haut le compte (avatar, points Hardcore et Softcore, lien vers le profil sur le site). Sur une fenêtre large, le jeu et le mode restent dans une colonne fixe à gauche, à côté des onglets. La liste des succès se filtre (tous, à obtenir, obtenus, avec leur nombre) et se cherche sans tenir compte des accents ; elle se trie par ordre recommandé, progression, date de déblocage ou rareté. Chaque succès indique sa date de déblocage, sa progression mesurée, sa rareté et son type (Manquable, Progression, Victoire) ; un clic ouvre sa fiche.
- **Classements** présente le top 20 ou les joueurs autour de votre position, avec votre rang et votre score. Les scores sont soumis en Hardcore. **Activité** affiche la Rich Presence calculée pendant la partie ; après fermeture du jeu, les données sont indiquées comme mémorisées.
- Les badges verrouillés et débloqués utilisent leurs images respectives. Les téléchargements échoués sont réessayés et le cache évince les anciennes images. Les notifications de succès s'affichent successivement ; leur durée est suspendue pendant le menu de pause.

La [validation RetroAchievements](docs/retroachievements-validation.md) précise les tests, les vérifications manuelles restantes et les conditions d'une approbation officielle. La [politique de confidentialité](docs/confidentialite.md) décrit les données locales et les échanges réseau.

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

Pour générer les mêmes paquets Unix après compilation :

```sh
python3 tools/package_unix.py --build build --output dist
```

L'outil nécessite `patchelf` et les métadonnées des paquets Debian/Ubuntu sous Linux ; sous macOS, il utilise CMake, les outils Xcode et les licences des formules Homebrew de compilation. Il rassemble les bibliothèques, corrige leurs chemins et vérifie les pages principales depuis un emplacement contenant des espaces. La CI Linux relance aussi le paquet dans une image Ubuntu minimale, sans installation des bibliothèques de l'application. Les services graphiques et audio réels restent à vérifier sur un poste de bureau.

## Licence et crédits

© 2026 GhostPunishR. RavenEmu est distribué sous licence [AGPL-3.0-only](LICENSE).

Le projet utilise RavenCore, SDL2, Dear ImGui, stb_image, libcurl et rcheevos. Ces dépendances conservent leurs licences respectives ; consultez [NOTICE.md](NOTICE.md) et leurs dépôts pour les détails.

RavenEmu est un projet indépendant, sans affiliation officielle avec Nintendo ou RetroAchievements.
