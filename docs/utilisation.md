# Utiliser RavenEmu

[Retour au README](../README.md)


1. Lancez l'application, puis ajoutez vos fichiers `.gb`, `.gbc` ou `.gba` à la bibliothèque. Vous pouvez aussi déposer un fichier ou un dossier dans la fenêtre.
2. Sélectionnez un jeu pour le lancer. Les ROM restent à leur emplacement d'origine.
3. Ouvrez **Réglages** pour consulter les dossiers de sauvegardes et de jaquettes, régler l'image et le son, ou changer les commandes. Le bouton **Mode TV** de la barre latérale passe en mode TV.
4. Ouvrez **Profil** pour vous connecter à RetroAchievements si vous souhaitez utiliser les succès.

Aucune ROM commerciale n'est fournie. L'émulation ne nécessite pas de compte RetroAchievements.

## Mode TV

Le bouton **Mode TV** de la barre latérale active la navigation sans curseur de souris.

Les indications montrent les boutons de la manette branchée (Xbox, PlayStation, Nintendo ou générique). Dans le mode TV, Start (ou Échap) ouvre un menu : **Reprendre**, **Mode bureau** ou **Quitter RavenEmu** ; une simple pression ne ramène plus au mode bureau. En mode TV, le lanceur et le jeu partagent la même fenêtre plein écran et le même rendu : aucune fenêtre ne s'ouvre ni ne se ferme entre les deux, le bureau n'apparaît jamais. Le mode TV vaut pour la session : il revient après une partie, mais RavenEmu démarre toujours en mode bureau. En jeu, un succès débloqué s'annonce par un bandeau en haut au centre, en mode TV comme en mode bureau, où il est plus compact : son badge descend, le bandeau s'ouvre de part et d'autre avec « Succès débloqué », les points et le titre, puis se replie dans le badge, qui remonte. Au lancement d'un jeu reconnu, le bandeau montre l'icône du jeu, son titre et si des succès sont disponibles. Un jeu maîtrisé ou terminé, les classements et les avis du service ont le même bandeau. La progression et les défis sont agrandis pour être lus à distance. Tout reste dans une marge de sécurité de l'écran.

## Commandes clavier en jeu

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

Ces touches sont les valeurs par défaut. Dans **Réglages > Commandes**, cliquez sur une touche puis appuyez sur celle souhaitée : boutons de la console et raccourcis d'émulation sont personnalisables et mémorisés pour les prochains lancements. Les touches déjà utilisées sont échangées. **Non attribuée** libère une commande et **Rétablir les touches par défaut** restaure l'ensemble. Échap annule une modification et reste toujours disponible pour ouvrir ou fermer le menu en jeu. Les indications du menu de pause suivent votre configuration.

Les boutons de la manette se configurent de la même façon, dans la colonne **Manette** : cliquez sur un bouton, puis appuyez sur celui souhaité, gâchettes LT / RT (L2 / R2, ZL / ZR) comprises. Les noms suivent la manette branchée. Un bouton déjà utilisé est échangé et **Non attribué** libère un bouton de la console. Select et Start restent toujours attribués, puisqu'ils ouvrent le menu ensemble. Le bouton Xbox, PS ou Home reste au système et ne peut pas être choisi. Échap annule. **Rétablir les commandes par défaut** restaure le clavier et la manette.

À la manette, **Select + Start** pressés ensemble, dans un ordre ou dans l'autre (par défaut Affichage + Menu sur Xbox, Share/Create + Options sur PlayStation, − + + sur Nintendo, ou les boutons que vous leur avez attribués), ouvrent ou ferment le menu en jeu, sans que le jeu reçoive ces boutons. Pour cela, un Select ou un Start seul n'est transmis au jeu qu'après 150 ms, ou dès qu'il est relâché. Le bouton Xbox, PS ou Home reste au système : Windows y ouvre la Xbox Game Bar et Steam son interface, qui s'afficheraient en même temps. Un menu ouvert à la manette n'affiche pas les raccourcis clavier. B (Rond sur PlayStation) revient en arrière : de **Succès et classements** au menu, puis du menu au jeu. Dans un jeu lancé depuis le mode TV, le menu en jeu est agrandi pour être lu à distance : l'entrée sélectionnée est surlignée, les boutons de la manette sont rappelés en bas, et l'entrée Plein écran disparaît puisque le mode TV reste en plein écran. **Succès et classements** s'y ouvre en grand panneau, chaque succès sur une ligne que la croix parcourt, avec LB / RB (L1 / R1) pour changer d'onglet. Sur un écran de moins de 900 lignes (720p, 768p), le menu se resserre pour tenir en entier.

## Image et son

Dans **Réglages > Affichage**, **Lisser l’image** adoucit les pixels quand l'image du jeu est agrandie ; désactivé, par défaut, les pixels restent nets comme sur la console. **Réglages > Son** règle le volume du jeu ou le coupe entièrement, carillon des succès compris. Ces réglages sont pris en compte au lancement du jeu suivant.

## Sauvegardes et jaquettes

Sous Windows et Linux, l'application utilise les dossiers `Sauvegardes` et `Jaquettes` à côté de l'exécutable. Si cet emplacement n'est pas accessible en écriture, elle essaie son dossier de données utilisateur. L'application `.app` macOS place ces dossiers dans les données utilisateur pour préserver sa signature ; une compilation macOS en exécutable simple garde le comportement portable. Les chemins utilisés sont affichés dans **Réglages**.

La bibliothèque et les réglages sont enregistrés séparément dans le dossier de données utilisateur. Copier une ROM ne transfère donc pas automatiquement sa progression : conservez également vos sauvegardes.

Les sauvegardes sont isolées dans un sous-dossier par chemin de ROM afin que deux jeux portant le même nom ne puissent pas écraser leur progression. Les anciens fichiers sont copiés vers ce classement et conservés à leur emplacement d'origine. Un ancien fichier partagé entre plusieurs ROM de même nom est attribué au premier jeu qui le migre ; vérifiez cette progression si vous aviez déjà des doublons. Si vous déplacez une ROM, conservez son ancien dossier de sauvegarde pour transférer sa progression.

## RetroAchievements

L'intégration repose sur le SDK [rcheevos](https://github.com/RetroAchievements/rcheevos). **Profil** et le menu de pause donnent accès aux succès, aux classements et à l'activité du jeu. **Actualiser les succès**, dans une fiche, récupère les données du compte et du jeu sans lancer l'émulation et sans ouvrir de session de jeu : le jeu n'apparaît pas comme joué sur votre profil public. Les dates de déblocage déjà connues sont conservées ; celles des succès obtenus ailleurs apparaissent après une partie. Les déblocages, défis et progressions mesurées s'affichent pendant la partie.

- Une connexion Internet et une ROM reconnue par RetroAchievements sont nécessaires pour les fonctions en ligne.
- **Softcore** est sélectionné au départ. **Mode Hardcore**, dans Profil, mémorise votre choix. Le menu de pause permet aussi de changer de mode. Passer en Hardcore réinitialise entièrement la console et bloque tous les chargements d'états, y compris pendant la connexion. Revenir en Softcore conserve la partie. Sauvegarder un état reste possible en Hardcore pour le débogage.
- Le Hardcore nécessite un compte connecté, une ROM reconnue et un coffre système disponible pour protéger les envois hors ligne. En attendant la connexion ou la reconnaissance, l'émulation attend ; le menu permet de revenir en Softcore. Une panne du journal protégé interrompt le Hardcore et indique le passage en Softcore. Quand le Hardcore ne peut pas être activé, le réglage repasse en Softcore pour que l'alerte ne revienne pas à chaque lancement. **Aucune approbation officielle Hardcore par RetroAchievements n'est revendiquée.**
- Le mot de passe n'est jamais sauvegardé. Sous Windows, le jeton de session est conservé dans le Gestionnaire d'identifiants de votre compte Windows pour vous reconnecter automatiquement au démarrage. **Déconnexion** ou **Oublier la session** efface cette connexion mémorisée.
- Sous macOS, le jeton est conservé dans le Trousseau. Sous Linux, la mémorisation utilise Secret Service via `libsecret` et nécessite un trousseau disponible et déverrouillé (GNOME Keyring, par exemple). Sans coffre disponible, la connexion reste utilisable pour la session en cours.
- En cas de panne réseau, la reconnexion par jeton et la recherche du jeu sont réessayées après 5, 10, 20, 40 puis 60 secondes au maximum. **Réessayer** permet une tentative immédiate. Un jeton expiré demande une nouvelle connexion. Un refus définitif du service au chargement d'un jeu (session ou accès refusé) n'est pas présenté comme une panne et n'est pas réessayé en boucle : une session refusée est renouvelée une fois, puis une nouvelle connexion est demandée.
- Un échec de connexion affiche le motif donné par RetroAchievements (mot de passe ou accès refusé), distinct d'un serveur qui ne répond pas.
- Les messages distinguent une ROM inconnue, un jeu reconnu sans succès et un serveur indisponible. Pendant le chargement d'un jeu, le profil et le menu de pause n'affichent que les données mémorisées de ce jeu, jamais celles du jeu précédent. Avec un coffre disponible, les succès et scores en attente sont conservés après fermeture dans un journal authentifié, puis repris avec le jeton actuel. Le profil indique le nombre d'envois en attente. Ils sont repris avant la recherche du jeu ; un envoi que le service n'accepte pas reste en attente sans empêcher le chargement des succès. Sans coffre, les reprises sont limitées à l'exécution en cours : gardez l'application ouverte jusqu'à la synchronisation.
- Les états Softcore conservent ensemble l'état de la console et les compteurs de conditions des succès, liés au compte et à l'empreinte de la ROM. Les anciens états restent lisibles ; leurs compteurs RetroAchievements sont réinitialisés.
- Les succès déjà obtenus sur votre compte sont récupérés avant le suivi de la partie, y compris ceux gagnés sur un autre émulateur. Si la vérification initiale échoue, le statut indique la panne et la synchronisation est réessayée ; une panne ne doit pas être présentée comme un historique vide. **Actualiser les succès** permet aussi de remettre à jour une ancienne fiche mémorisée.
- Le profil public et les succès des ROMs déjà reconnues sont mémorisés par compte et indiqués comme données mémorisées. Le panneau de droite, le résumé d'accueil et le mode TV affichent les compteurs du jeu sélectionné ; **Voir tous les succès** ouvre sa liste dans Profil. Ces données seules ne permettent ni de se connecter ni de débloquer un succès. **Déconnexion**, dans l'en-tête du compte, demande une confirmation qui indique le nombre d'envois encore en attente, puis efface le jeton, le cache, ces envois et leur clé de protection.
- **Profil** garde en haut le compte (avatar, points Softcore et Hardcore, lien vers le profil sur le site). Sur une fenêtre large, le jeu et le mode restent dans une colonne fixe à gauche, à côté des onglets. La liste des succès se filtre (tous, à obtenir, obtenus, avec leur nombre) et se cherche sans tenir compte des accents ; elle se trie par ordre recommandé, progression, date de déblocage ou rareté. Chaque succès indique sa date de déblocage, sa progression mesurée et sa rareté, ainsi que son envoi : confirmé par RetroAchievements, en attente ou refusé. Un clic ouvre sa fiche, qui précise aussi son type (Manquable, Progression, Victoire).
- **Mes jeux**, dans Profil, résume la progression de chaque jeu reconnu de la bibliothèque : succès et points obtenus, jeux maîtrisés (tous les succès en Hardcore) ou complétés (tous en Softcore), tri par jeux récents, progression ou nom. Ces données viennent du cache par ROM et restent consultables hors ligne ; un jeu jamais lancé ni actualisé n'y figure pas encore. Un clic ouvre les succès du jeu.
- **Classements** présente le top 20 ou les joueurs autour de votre position, avec votre rang et votre score. Les scores sont soumis en Hardcore. **Activité** affiche la Rich Presence calculée pendant la partie ; après fermeture du jeu, les données sont indiquées comme mémorisées.
- Les badges verrouillés et débloqués utilisent leurs images respectives. Les téléchargements échoués sont réessayés et le cache évince les anciennes images. Les notifications de succès s'affichent successivement, en haut au centre, comme les autres messages en jeu (état sauvegardé, erreurs), placés juste en dessous ; leur durée est suspendue pendant le menu de pause.
- Un court carillon accompagne chaque succès débloqué et chaque jeu maîtrisé ou terminé. Il est créé par RavenEmu, sans fichier son, et joué avec le son du jeu. **Son des succès**, dans Profil, le coupe ; le choix s'applique à la partie suivante. **Couper le son**, dans Réglages > Son, coupe aussi le carillon.

La [validation RetroAchievements](retroachievements-validation.md) précise les tests, les vérifications manuelles restantes et les conditions d'une approbation officielle. La [politique de confidentialité](confidentialite.md), aussi accessible depuis Réglages > À propos, décrit les données locales et les échanges réseau.
