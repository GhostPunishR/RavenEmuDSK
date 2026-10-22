[![CI](https://img.shields.io/github/actions/workflow/status/GhostPunishR/RavenEmuDSK/build.yml?branch=main&style=plastic&label=CI)](https://github.com/GhostPunishR/RavenEmuDSK/actions/workflows/build.yml)
[![Anthropic](https://img.shields.io/badge/Anthropic-Claude-D97757?style=plastic&logo=anthropic&logoColor=white)](https://www.anthropic.com/)
[![Licence AGPL-3.0](https://img.shields.io/badge/Licence-AGPL--3.0-663399?style=plastic&logo=gnu&logoColor=white)](LICENSE)

# RavenEmu

**La version PC de RavenEmu pour Game Boy, Game Boy Color et Game Boy Advance.**

Application open source pour **Windows, Linux et macOS**, écrite en C++20 avec SDL2, Dear ImGui et les moteurs [RavenCore](https://github.com/GhostPunishR/RavenCore). Le projet est en développement.

## Fonctionnalités

- Bibliothèque avec jaquettes, import par dossier, glisser-déposer et jeux récents.
- Mode TV, clavier et manette personnalisables, volume réglable et lissage de l’image.
- Sauvegardes de progression, états rapides et plein écran.
- RetroAchievements : profil, succès, classements et modes Softcore / Hardcore.

## Télécharger et jouer

Les compilations sont disponibles dans les **Artifacts** des exécutions réussies de [GitHub Actions](https://github.com/GhostPunishR/RavenEmuDSK/actions/workflows/build.yml). Une connexion GitHub est nécessaire pour télécharger ces artefacts. Aucune ROM n'est fournie.

1. Téléchargez le paquet de votre système et extrayez-le entièrement.
2. Lancez `RavenEmuDSK.exe` sous Windows, `RavenEmu/RavenEmuDSK` sous Linux ou `RavenEmu.app` sous macOS.
3. Ajoutez vos ROMs `.gb`, `.gbc` ou `.gba`. Le compte RetroAchievements est facultatif.

Consultez le [guide d'installation](docs/installation.md) pour les prérequis et la compilation depuis les sources.

## Documentation

- [Utilisation, commandes, sauvegardes et RetroAchievements](docs/utilisation.md)
- [Développement, tests et création des paquets](docs/developpement.md)
- [Validation RetroAchievements et limites actuelles](docs/retroachievements-validation.md)
- [Confidentialité et données locales](docs/confidentialite.md)

Le Hardcore est implémenté ; aucune approbation officielle RetroAchievements n'est revendiquée.

## Contribuer

Le [guide de contribution](CONTRIBUTING.md) explique comment signaler un problème, préparer une modification et la tester. Les modèles de tickets et de pull requests sont inclus dans le dépôt.

## Licence

© 2026 GhostPunishR. Code sous licence [AGPL-3.0-only](LICENSE). Les dépendances conservent leurs licences respectives : [NOTICE.md](NOTICE.md).

RavenEmu est indépendant, sans affiliation officielle avec Nintendo ou RetroAchievements. Le dépôt et l'exécutable conservent le nom technique `RavenEmuDSK`.
