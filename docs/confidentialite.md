# Confidentialité de RavenEmu

RavenEmu ne contient ni publicité, ni analyse d'utilisation, ni envoi automatique de rapports de plantage. Le projet n'exploite aucun serveur de télémétrie ou d'entreposage des données de l'application. L'émulation locale reste utilisable sans compte et sans Internet.

## Données conservées sur votre ordinateur

- Bibliothèque, chemins de ROMs, réglages, historique local, durées de jeu, sauvegardes et captures : conservés jusqu'à suppression par l'utilisateur. Les dossiers sont indiqués dans Réglages. ROMs et sauvegardes ne sont pas téléversées par l'intégration RetroAchievements.
- Compte RetroAchievements : nom et jeton de session dans le Gestionnaire d'identifiants Windows, le Trousseau macOS ou Secret Service Linux. Le mot de passe sert uniquement à la connexion HTTPS et n'est jamais sauvegardé. Déconnexion ou Oublier la session supprime le jeton.
- Cache public : nom affiché, avatar, points, jeux identifiés, succès avec leur date de déblocage, leur type et leur rareté, mode des compteurs, métadonnées des classements et dernière activité. Il reste local jusqu'à déconnexion ou suppression manuelle. Il ne contient aucun jeton et ne donne aucune autorisation d'accès au compte.
- Envois en attente : compte, empreinte du jeu, identifiant du succès ou classement, mode, score éventuel et date. Avec un coffre disponible, un journal les authentifie avec une clé conservée séparément dans le coffre. Les entrées sont retirées après acquittement ou refus définitif. Elles restent conservées pendant une panne et sont supprimées à la déconnexion explicite, même sans avoir été envoyées.
- États Softcore : compteurs de conditions, nom du compte et empreinte du jeu accompagnent l'état du moteur. Conservés avec les sauvegardes jusqu'à suppression ; aucun jeton n'y est inclus.
- Images RetroAchievements : cache en mémoire, limité à 128 images et supprimé à la fermeture.

Le cache et le journal se trouvent dans `%APPDATA%/RavenEmu/RetroAchievements` sous Windows, `~/Library/Application Support/RavenEmu/RetroAchievements` sous macOS, et `$XDG_DATA_HOME/RavenEmu/RetroAchievements` ou `~/.local/share/RavenEmu/RetroAchievements` sous Linux. Le journal utilise `pending`. Les noms de fichiers dérivent d'empreintes pour séparer comptes et chemins.

## Échanges réseau

**RetroAchievements**, lorsque vous connectez votre compte : authentification, profil, identification des jeux par empreinte, succès, déblocages, scores et Rich Presence calculée à partir de la mémoire émulée. Actualiser un jeu depuis la bibliothèque lit seulement le profil, les données du jeu et vos succès : aucune session de jeu ni Rich Presence n'est envoyée. Ces échanges utilisent HTTPS avec `retroachievements.org`. Avatars et badges proviennent des hôtes d'images RetroAchievements autorisés. Le service reçoit aussi l'adresse IP et l'agent utilisateur, le même pour toutes les requêtes de RavenEmu : version de RavenEmu, système d'exploitation avec sa version et l'architecture, versions de RavenCore et de rcheevos, par exemple `RavenEmu/0.2.0 (Windows 10.0.26100; x64) RavenCore/0.7.1 rcheevos/12.5`. Il ne contient aucun identifiant de compte ni de machine. Les données et leur conservation côté service relèvent de sa [politique de confidentialité](https://retroachievements.org/privacy.php).

**GitHub**, lors d'une vérification de mise à jour : demande des métadonnées de la dernière version publique de `GhostPunishR/RavenEmuDSK`, avec l'agent utilisateur. Aucun compte RetroAchievements, chemin de ROM, sauvegarde ou statistique de jeu n'est transmis pour cette vérification. Le téléchargement ouvre GitHub dans le navigateur ; ses journaux et téléchargements relèvent de sa [politique de confidentialité](https://docs.github.com/fr/site-policy/privacy-policies/github-general-privacy-statement).

Les serveurs contactés appartiennent à ces prestataires ; leurs pays d'hébergement et transferts internationaux dépendent de leurs infrastructures et politiques. RavenEmu ne fixe ni ne vérifie ces lieux et n'exploite aucun serveur intermédiaire. Aucun entrepôt de télémétrie propre au projet n'est utilisé. Cette documentation ne revendique aucune validation juridique ni approbation de confidentialité par RetroAchievements.

Les liens vers des sites s'ouvrent dans votre navigateur et suivent les politiques de ces sites. Pour effacer les données locales RetroAchievements, déconnectez le compte puis supprimez le cache si nécessaire ; sauvegardes, captures et états restent gérés séparément. La suppression du compte ou des données déjà transmises doit être demandée au prestataire concerné.
