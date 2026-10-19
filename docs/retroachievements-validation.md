# Validation RetroAchievements

RavenEmu utilise le SDK rcheevos fixé par CMake. Deux corrections locales contrôlées dans `cmake/rcheevos_safety.cmake` sécurisent la notification de fin de jeu pendant un déchargement et les réponses de succès arrivant après fermeture/changement du jeu. CMake génère une copie corrigée, sans modifier le dépôt de la dépendance ; une mise à jour du SDK doit réexaminer ces corrections. Le mode disponible est Softcore. Aucun test automatique décrit ici ne contacte un vrai compte RetroAchievements ni n'attribue de succès sur le service public.

## Tests automatiques

| Test | Couverture |
| --- | --- |
| `retro_session` | Mot de passe utilisé uniquement pour l'authentification, jeton sauvegardé et renouvelé, reconnexion au redémarrage, temporisation après panne, expiration, déconnexion pendant une réponse en vol, erreurs du coffre et profil mémorisé hors ligne. |
| `retro_game_integration` | SDK réel et moteurs RavenCore GB, GBC et GBA, petites ROMs synthétiques écrites dans le test, identification simulée, lecture mémoire, succès déclenché, badge verrouillé puis coloré, envoi interrompu puis retransmis sans double notification, fermeture et déconnexion avec des envois actifs ou programmés. ROM inconnue, jeu sans succès et panne sont distincts. |
| `retro_profile_store` | Cache public par compte et par chemin complet de ROM, deux éditions de même nom, compatibilité du cache précédent, remplacement du fichier, fichier corrompu, effacement ; aucun état d'authentification ou de progression active restauré. |
| `home_grid` | Panneau de droite : compteurs, points et liste du jeu sélectionné après redémarrage, changement de ROM, absence de données, lien vers le bon jeu du profil et effacement à la déconnexion. |
| `retro_badge_cache` | Échec puis retéléchargement, attente progressive, remplacement dans un cache plein, conservation des textures utilisées par l'image en cours, deux téléchargements simultanés au maximum et filtrage des URL. |
| `retro_notifications` | 32 déblocages successifs, doublons, indicateurs de défis et de classements, délai maximal d'attente d'un badge, pause de dix minutes et réinitialisation d'une progression mesurée. |
| `windows_session_store` | Gestionnaire d'identifiants Windows : création, lecture après recréation du stockage, mise à jour et suppression. La CI peut ignorer ce test si Windows ne fournit aucune session utilisateur compatible. |
| `native_session_store` | Même cycle avec les coffres macOS et Linux, exécuté explicitement dans la CI avec un coffre de test isolé et des identifiants synthétiques. |

Les tests du SDK injectent uniquement le transport HTTP et l'horloge ; l'analyse des réponses, les conditions des succès et les nouvelles tentatives d'envoi restent exécutées par rcheevos. La CI compile et lance les tests sur Ubuntu, Windows et macOS. Un résultat local Linux ne vaut pas validation des deux autres systèmes.

## Vérifications manuelles avant une version publique

À réaliser dans une compilation native avec son propre compte et des ROMs obtenues légalement. Ne transmettez ni mot de passe ni jeton dans un ticket ou un journal.

- Sur Windows, se connecter depuis Profil, fermer puis relancer : reconnexion sans saisir de mot de passe. Vérifier le même scénario sous macOS et Linux avec le coffre déverrouillé.
- Ouvrir un jeu reconnu GB, GBC puis GBA : titre et liste officiels, compteurs, progression mesurée et déblocage visibles. Vérifier le succès sur le site du service.
- Couper puis rétablir le réseau sans fermer l'application : message hors ligne, envoi repris et absence de notification dupliquée. Fermer pendant une panne n'assure pas la conservation des envois.
- Vérifier une ROM inconnue et un jeu reconnu sans succès. Une ROM inconnue ne doit pas provoquer des requêtes répétées.
- Redémarrer hors ligne : profil et dernier jeu indiqués comme mémorisés, aucun état connecté accordé. Rétablir le réseau : points actualisés après authentification et succès actualisés à l'ouverture du jeu.
- Déconnecter : le jeton et le cache du compte sont supprimés. Un jeton révoqué sur le site doit inviter à une nouvelle connexion.
- Déclencher plusieurs succès pendant une partie et laisser le menu de pause ouvert : les cartes doivent conserver leur durée visible et leur ordre.

## Audit Hardcore

Référence : [exigences officielles de conformité Hardcore](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html), consultées dans le [dépôt officiel](https://github.com/RetroAchievements/docs/blob/9de47ca8af23bc4fb231f6b0d9261164305d0704/docs/general/hardcore-compliance-requirements.md).

Les chemins clavier Échap/P et manette Guide consultent désormais la même autorisation de pause du SDK. Les chargements d'états sont bloqués côté exécution et les commandes du menu sont désactivées en Hardcore. Les échecs de réinitialisation sont vérifiés et les entrées maintenues sont relâchées. L'agent utilisateur identifie RavenEmu et la version de RavenCore réellement liée.

L'API publique refuse actuellement l'activation du Hardcore. Ces protections ne constituent pas une certification. Restent notamment à valider avant ouverture :

- Sessions avec de vrais jeux et comptes, Rich Presence et classements, lectures mémoire et contraintes de pause sur chaque plateforme.
- File d'envois hors ligne persistante et protégée entre deux lancements : actuellement, les reprises du SDK couvrent uniquement l'exécution en cours.
- Conservation des compteurs de conditions dans les états Softcore : actuellement, un chargement réinitialise les conditions du SDK.
- Absence de contournement par chargement d'état, ralentissement, avance image, rembobinage, triche ou reprise rapide ; toute future activation à partir du Softcore devra imposer une réinitialisation complète.
- Documentation de confidentialité, distribution et compatibilité des sauvegardes, puis démarche de validation auprès de RetroAchievements. Les exigences officielles incluent aussi une ancienneté publique minimale du projet ; aucune approbation n'est revendiquée ici.

## Données locales

Le coffre du système contient uniquement le nom de compte et le jeton de session. Aucun mot de passe n'est sauvegardé. Le cache séparé contient le nom public, l'avatar, les points et le dernier jeu et les succès des ROMs déjà synchronisées ; il ne contient aucun jeton ni autorisation d'authentification. Il est conservé jusqu'à une déconnexion ou une suppression manuelle.

Emplacements du cache : `%APPDATA%/RavenEmu/RetroAchievements` sur Windows, `~/Library/Application Support/RavenEmu/RetroAchievements` sur macOS, `$XDG_DATA_HOME/RavenEmu/RetroAchievements` ou `~/.local/share/RavenEmu/RetroAchievements` sur Linux. Les fichiers portent une empreinte du compte pour éviter les collisions de chemins. Les badges sont conservés uniquement en mémoire (128 entrées au maximum).

Le panneau de droite retrouve les succès par chemin complet de ROM, sans comparer les titres ni les noms de fichiers. Les résumés sont conservés dans un sous-dossier par compte et mis à jour à la reconnaissance du jeu et lors des déblocages. Après déplacement d'une ROM, lancez-la pour associer son nouveau chemin. Les anciens caches sans chemin de ROM restent lisibles dans Profil ; un premier lancement avec cette version crée l'association nécessaire au panneau.
