# Validation RetroAchievements

RavenEmu utilise le SDK rcheevos fixé par CMake. Deux corrections locales contrôlées dans `cmake/rcheevos_safety.cmake` sécurisent la notification de fin de jeu pendant un déchargement et les réponses arrivant après fermeture ou changement du jeu. CMake génère une copie corrigée, sans modifier le dépôt de la dépendance ; une mise à jour du SDK doit réexaminer ces corrections.

Softcore et Hardcore sont implémentés. Aucun test automatique décrit ici ne contacte un vrai compte RetroAchievements ni n'attribue de succès sur le service public. Ces tests ne constituent pas une approbation officielle du client.

## Tests automatiques

| Test | Couverture |
| --- | --- |
| `retro_session` | Authentification, jeton sauvegardé et renouvelé, reconnexion au redémarrage, attente progressive après panne, expiration, déconnexion pendant une réponse, erreurs du coffre et cache hors ligne. |
| `retro_game_integration` | SDK réel et moteurs RavenCore GB/GBC/GBA avec petites ROMs synthétiques : identification, lecture mémoire, déblocage, badges, retransmission sans double notification, fermeture et déconnexion avec envois actifs ou programmés. ROM inconnue, jeu sans succès et panne sont distincts. |
| `retro_features` | Recréation du client après un succès hors ligne, reprise avec nouveau jeton et date d'origine avant récupération des succès ; compteurs réels du SDK dans les états Softcore, connexion différée, corruption et liaison compte/ROM ; Hardcore GB/GBC/GBA avec réinitialisation synchrone, chargement bloqué avant mutation, sauvegarde de débogage, déblocage et score ; activation pendant le chargement ou la connexion, renouvellement d'un jeton refusé ; actualisation depuis une ROM sans moteur, classements, erreur et annulation, Rich Presence et cache public. |
| `retro_outbox` | Persistance, dédoublonnage, référence HMAC-SHA256 calculée indépendamment, modification et troncature des fichiers, clé manquante, erreur du coffre et effacement à la déconnexion. |
| `retro_profile_store` | Cache public par compte et chemin complet de ROM, éditions homonymes, compatibilité des anciennes versions, remplacement, corruption et effacement. Aucune authentification restaurée depuis le cache. |
| `game_overlay` | Navigation clavier du menu de pause en grande et petite fenêtre, sauvegarde de débogage accessible, chargement ignoré en Hardcore, ouverture des succès/classements et changement de mode. |
| `home_grid` | Résumés du jeu sélectionné après redémarrage, changement de ROM, absence de données, lien vers le bon jeu du profil, effacement à la déconnexion et survol sans conflits d'identifiants ImGui. |
| `retro_badge_cache` | Téléchargements échoués et réessayés, cache plein, durée de vie des textures, deux téléchargements simultanés au maximum et filtrage des URL. |
| `retro_notifications` | Déblocages successifs, doublons, indicateurs de défis et classements, attente bornée d'un badge, pause et réinitialisation d'une progression mesurée. |
| `windows_session_store` | Création, lecture après recréation, mise à jour et suppression dans le Gestionnaire d'identifiants Windows. La CI peut ignorer ce test si le système ne fournit pas de session compatible. |
| `native_session_store` | Même cycle sous macOS et Linux avec un coffre isolé et des identifiants synthétiques dans la CI. |

Les tests injectent le transport HTTP, les coffres et l'horloge ; l'analyse des réponses, l'évaluation des conditions, leur sérialisation et les reprises restent exécutées par rcheevos. La CI compile et teste Ubuntu, Windows et macOS. Un résultat local Linux ne vaut pas validation des autres systèmes.

## Protections Hardcore

Référence : [exigences officielles](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html), consultées dans le [dépôt officiel](https://github.com/RetroAchievements/docs/blob/9de47ca8af23bc4fb231f6b0d9261164305d0704/docs/general/hardcore-compliance-requirements.md).

- Softcore par défaut ; choix mémorisé depuis Profil, Réglages > RetroAchievements ou le menu de pause. Indication Hardcore visible en jeu.
- Compte connecté, ROM reconnue et journal protégé exigés. L'émulation attend une authentification ou identification incomplète ; le menu permet de revenir en Softcore. Une panne du serveur après reconnaissance laisse les succès s'accumuler dans la file hors ligne.
- Softcore vers Hardcore : réinitialisation du moteur avant toute évaluation Hardcore, même pendant un chargement ou une connexion différée. Une session renouvelée en Hardcore repart après réinitialisation. Une erreur de réinitialisation annule le mode.
- Tous les chargements d'états passent par une garde commune avant décodage ou mutation du moteur. Clavier et menu bloquent également la commande, y compris quand le choix Hardcore attend la connexion.
- La sauvegarde d'états reste permise en Hardcore pour débogage. Charger redevient possible en Softcore. Un état ne réactive jamais automatiquement le Hardcore.
- Échap, P et le bouton Guide de la manette consultent l'autorisation de pause du SDK. La réinitialisation relâche les entrées et ferme le menu de pause.
- Aucun moteur de triche, rembobinage, ralentissement, avance image ou lecture d'entrées enregistrées n'est exposé par l'application. Ces protections devront être réexaminées si de telles fonctions sont ajoutées.
- Rich Presence et classements restent actifs en Hardcore ; les listes et résultats sont accessibles dans le menu. L'agent utilisateur identifie RavenEmu, sa version numérique et RavenCore.
- Journal authentifié par HMAC-SHA256, avec clé aléatoire de 32 octets dans le coffre système. Un fichier altéré ou une clé absente bloque la reprise. Une erreur de persistance interrompt le Hardcore avant l'image suivante et invite à garder l'application ouverte pour l'envoi en cours.

## Vérifications manuelles avant une version publique

À réaliser dans une compilation native avec son propre compte et des ROMs obtenues légalement. Ne transmettez ni mot de passe ni jeton dans un ticket ou un journal.

- Sur chaque système : connexion, fermeture, reconnexion automatique, coffre verrouillé ou indisponible, déconnexion et effacement effectif.
- Jeux réels GB/GBC/GBA dans les deux modes : lectures mémoire, liste officielle, progression mesurée, indicateurs de défis et déblocage confirmé sur le site.
- Couper le réseau après reconnaissance, débloquer, fermer et relancer hors ligne puis rétablir le réseau : un seul succès enregistré, points actualisés, aucun doublon. Vérifier aussi un score en attente et un refus explicite du service.
- Actualiser une fiche sans jouer : points et succès récents corrects, ROM absente et panne expliquées. Comparer top 20, position personnelle, record et Rich Presence au site.
- Tester F9 et le menu de chargement en Hardcore pendant une connexion et après un état Softcore. Passer en Hardcore avant, pendant et après identification : mémoire réinitialisée. Vérifier pause SDK et manette.
- Sauvegarder au milieu d'une condition à plusieurs occurrences en Softcore, redémarrer et charger : compteur et mémoire restaurés ensemble. Tester un ancien état, la corruption et un autre compte ou jeu.
- Comparer les sauvegardes batterie avec d'autres émulateurs compatibles : SRAM, EEPROM, Flash et RTC. Formats et moteur restent du ressort de RavenCore.
- Vérifier les petites fenêtres, la navigation clavier/manette, la liste en jeu, les scores, les indicateurs et l'activité mémorisée après fermeture.

La validation sur de vrais comptes et jeux reste à effectuer. L'approbation officielle nécessite une démarche auprès de RetroAchievements et, selon leurs exigences, au moins six mois de disponibilité publique. Aucune approbation n'est revendiquée. Le SDK est intégré directement ; RAIntegration et ses outils de développement Windows ne sont pas inclus.

## Données locales et reprise

Voir la [politique de confidentialité](confidentialite.md). Le coffre conserve le jeton de session et, séparément, la clé du journal. Aucun mot de passe n'est enregistré. Le journal ne contient ni jeton ni mot de passe : compte, empreinte du jeu, identifiant, mode, score éventuel et date d'origine permettent de reconstruire une requête officielle avec le jeton actuel.

L'envoi est journalisé avant la requête. Les reprises du SDK gardent la même identité ; après redémarrage, deux envois au maximum sont repris en parallèle, avec attente progressive après panne. Une entrée est retirée après acquittement ou refus définitif du service. Un refus d'authentification conserve les données ; une expiration déclenche une reconnexion par jeton. La déconnexion explicite supprime aussi les envois non synchronisés. Sans coffre, le SDK assure seulement les reprises pendant l'exécution et l'interface l'indique.

Le cache public version 3 conserve les compteurs par chemin de ROM, leur mode, les métadonnées des classements et la dernière activité ; les versions 1 et 2 restent lisibles. Déplacer une ROM nécessite de la lancer ou de l'actualiser pour associer son nouveau chemin. Le cache ne donne aucune autorisation d'authentification.

Les états version 2 réunissent moteur et compteurs dans un fichier avec contrôle SHA256 ; la progression du SDK est liée au compte et à l'empreinte du jeu. Un état ancien réinitialise les compteurs. Un état corrompu est rejeté ; des compteurs d'un autre compte ou jeu sont ignorés et réinitialisés. Cette empreinte détecte la corruption et n'autorise jamais le chargement en Hardcore.
