# Contribuer à RavenEmu

Merci de contribuer au projet. Les corrections, améliorations de l'interface, tests et contributions à la documentation sont les bienvenus. Les échanges peuvent se faire en français ou en anglais.

## Signaler un problème ou proposer une idée

Consultez les [tickets existants](https://github.com/GhostPunishR/RavenEmuDSK/issues) pour éviter les doublons, puis utilisez le [formulaire adapté](https://github.com/GhostPunishR/RavenEmuDSK/issues/new/choose).

Pour un bug, indiquez votre système, la version ou le commit testé, les étapes de reproduction et le résultat attendu. Pour RetroAchievements, précisez le jeu, le mode Softcore ou Hardcore et si le problème apparaît avant ou après le lancement du jeu.

Ne joignez pas de ROM commerciale, de mot de passe, de jeton de session ni de fichier du coffre système. Masquez les informations personnelles dans les captures et journaux. Une ROM de test redistribuable peut être utilisée avec sa provenance et sa licence.

Pour une évolution importante, un ticket permet de discuter du besoin et du périmètre avant de commencer.

## Préparer votre environnement

1. Créez un fork du dépôt, puis clonez votre fork.
2. Suivez le [guide d'installation et de compilation](docs/installation.md) de votre système. Les dépendances téléchargées par CMake sont déjà fixées à des révisions précises.
3. Créez une branche pour votre modification :

```sh
git switch -c correction/description-courte
```

La [documentation de développement](docs/developpement.md) décrit les modules, les tests et la création des paquets. Pour une correction du moteur d'émulation, consultez aussi le dépôt [RavenCore](https://github.com/GhostPunishR/RavenCore).

## Réaliser la modification

- Gardez un objectif précis par pull request et évitez les reformatages sans rapport.
- Suivez le style C++20 et l'organisation des fichiers voisins. Utilisez le tiret simple `-` dans les textes, conformément au contrôle du dépôt.
- Affichez le nom **RavenEmu** dans l'interface. Préservez le logo fourni et documentez les nouvelles dépendances et leurs licences.
- Pour une correction de comportement, ajoutez ou adaptez un test qui reproduit le problème lorsque c'est pertinent. Pour un changement visuel, fournissez une capture et décrivez la vérification manuelle.
- Mettez à jour le guide concerné si les commandes, les réglages, le stockage ou le comportement utilisateur changent.
- Pour RetroAchievements, préservez les protections Hardcore et la confidentialité ; consultez la [validation dédiée](docs/retroachievements-validation.md) avant de modifier ces parcours.

## Vérifier avant l'envoi

Après avoir compilé, lancez depuis la racine du dépôt :

```sh
ctest --test-dir build --output-on-failure
python3 tools/check_text_style.py
python3 tools/check_ra_navigation.py
git diff --check
```

Avec Visual Studio, utilisez `ctest --test-dir build -C Release --output-on-failure`. Sous Windows, la commande Python peut s'appeler `python`. Le contrôle de typographie inspecte les fichiers suivis par Git ; ajoutez vos nouveaux fichiers à l'index avant de le lancer.

Pour une modification uniquement documentaire, les vérifications de liens, de typographie et de diff suffisent ; indiquez que vous n'avez pas relancé les tests C++. Pour une modification de l'application, exécutez les tests concernés et décrivez les parcours vérifiés manuellement. Signalez les vérifications impossibles sur votre système.

La [CI](https://github.com/GhostPunishR/RavenEmuDSK/actions/workflows/build.yml) compile et teste Windows, Linux et macOS. Corrigez les échecs liés à votre modification avant de demander sa fusion.

## Ouvrir une pull request

1. Relisez le diff et créez un commit avec un message décrivant le changement.
2. Poussez votre branche dans votre fork.
3. Ouvrez une pull request vers `main` et remplissez le modèle : problème, changement, ticket associé et validation.
4. Répondez aux retours et précisez les limites restantes.

N'incluez pas de dossiers `build` ou `dist`, de binaires générés, de données personnelles ou de sauvegardes de jeu.

## Licence des contributions

En proposant une contribution, vous acceptez sa distribution sous la licence [AGPL-3.0-only](LICENSE) du projet. Vous devez disposer des droits nécessaires sur le code et les ressources ajoutés ; conservez les mentions de licence des éléments tiers.
