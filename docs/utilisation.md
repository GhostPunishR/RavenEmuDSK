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
| Emplacement d'état précédent / suivant | F6 / F7 |
| Réinitialiser le jeu | F4 |
| Plein écran | F11 |
| Avance rapide (maintenir) | Espace |
| Retour arrière (maintenir) | R |
| Capture d'écran | F8 |
| Turbo A / Turbo B (maintenir) | Non attribuées |

Ces touches sont les valeurs par défaut. Dans **Réglages > Commandes**, cliquez sur une touche puis appuyez sur celle souhaitée : boutons de la console et raccourcis d'émulation sont personnalisables et mémorisés pour les prochains lancements. Les touches déjà utilisées sont échangées. **Non attribuée** libère une commande et **Rétablir les touches par défaut** restaure l'ensemble. Échap annule une modification et reste toujours disponible pour ouvrir ou fermer le menu en jeu. Les indications du menu de pause suivent votre configuration.

**Turbo A** et **Turbo B**, maintenus, appuient sur A ou B dix fois par seconde : trois images appuyé, trois relâché. Ils ne sont attribués ni au clavier ni à la manette au départ, pour ne prendre aucune touche déjà choisie : attribuez-les dans **Réglages > Commandes**, par exemple aux boutons X et Y de la manette, qui ne servent pas. Une gâchette attribuée au turbo ne fait plus l'avance rapide ni le retour arrière.

Les boutons de la manette se configurent de la même façon, dans la colonne **Manette** : cliquez sur un bouton, puis appuyez sur celui souhaité, gâchettes LT / RT (L2 / R2, ZL / ZR) comprises. Les noms suivent la manette branchée. Un bouton déjà utilisé est échangé et **Non attribué** libère un bouton de la console. Select et Start restent toujours attribués, puisqu'ils ouvrent le menu ensemble. Le bouton Xbox, PS ou Home reste au système et ne peut pas être choisi. Échap annule. **Rétablir les commandes par défaut** restaure le clavier et la manette.

À la manette, **Select + Start** pressés ensemble, dans un ordre ou dans l'autre (par défaut Affichage + Menu sur Xbox, Share/Create + Options sur PlayStation, − + + sur Nintendo, ou les boutons que vous leur avez attribués), ouvrent ou ferment le menu en jeu, sans que le jeu reçoive ces boutons. Pour cela, un Select ou un Start seul n'est transmis au jeu qu'après 150 ms, ou dès qu'il est relâché. Le bouton Xbox, PS ou Home reste au système : Windows y ouvre la Xbox Game Bar et Steam son interface, qui s'afficheraient en même temps. Un menu ouvert à la manette n'affiche pas les raccourcis clavier. B (Rond sur PlayStation) revient en arrière : de **Succès et classements** au menu, puis du menu au jeu. Dans un jeu lancé depuis le mode TV, le menu en jeu est agrandi pour être lu à distance : l'entrée sélectionnée est surlignée, les boutons de la manette sont rappelés en bas, et l'entrée Plein écran disparaît puisque le mode TV reste en plein écran. **Succès et classements** s'y ouvre en grand panneau, chaque succès sur une ligne que la croix parcourt, avec LB / RB (L1 / R1) pour changer d'onglet. Sur un écran de moins de 900 lignes (720p, 768p), le menu se resserre pour tenir en entier.

## Image et son

Dans **Réglages > Affichage** :

- **Lisser l’image** adoucit les pixels quand l'image du jeu est agrandie ; désactivé, par défaut, les pixels restent nets comme sur la console.
- **Mise à l’échelle entière** agrandit l'image un nombre entier de fois : chaque pixel de la console a la même taille, avec des bandes noires plus larges.
- **Couleurs de l’écran LCD** donne aux jeux Game Boy Color et Game Boy Advance des couleurs moins saturées et un peu plus sombres, comme sur l'écran de la console (la même correction que RavenEmu Android).
- **Mélange d’images** atténue le scintillement des jeux qui affichent un objet une image sur deux : **Mélange** montre la moyenne des deux dernières images, **Rémanence** laisse l'image précédente s'effacer lentement, comme sur un écran LCD.

Les captures d'écran et les vignettes des états montrent l'image avec ces réglages. **Réglages > Son** règle le volume du jeu ou le coupe entièrement, carillon des succès compris, et :

- **Sortie audio** : le casque, les haut-parleurs ou la télévision. Par défaut, le son suit la sortie choisie dans le système. Si la sortie choisie est débranchée au lancement du jeu, un message le dit et le son passe par celle du système.
- **Latence du son** : le son que RavenEmu prépare d'avance, 25, 50 (par défaut) ou 100 ms. 25 ms rapproche le son de l'image ; si le son grésille sur un ordinateur occupé, revenez à 50 ms. 100 ms convient à un son qui craque ou à un casque Bluetooth.

Ces réglages sont pris en compte au lancement du jeu suivant.

L'image et le son restent synchronisés :

- quand l'écran rafraîchit à un multiple de la fréquence de la console (59,73 images par seconde), à 5 % près, RavenEmu cale le jeu sur l'écran : une image du jeu à chaque rafraîchissement à 60 Hz, une tous les deux à 120 Hz, une tous les quatre à 240 Hz, sans image affichée plus longtemps qu'une autre. Le jeu tourne alors 0,46 % plus vite, ce qui ne s'entend pas ;
- le son est rééchantillonné, et ajusté d'au plus 0,5 % pour garder son avance (50 ms par défaut, **Latence du son**) : ni craquements ni décalage qui grandit ;
- sur un écran qui ne s'y prête pas (75, 90, 144 ou 165 Hz) en fenêtre, ou sans synchronisation verticale, le jeu garde sa propre cadence, avec le même ajustement du son.

### Écran et latence

Dans **Réglages > Affichage**, la carte **Écran et latence** règle le matériel :

- **Écran du jeu et du mode TV** : l'écran où s'ouvrent les jeux et le mode TV, par exemple une télévision branchée en second écran. Le bureau de RavenEmu reste sur l'écran principal.
- **Fréquence adaptée en plein écran**, activée par défaut : sur un écran à 144 ou 165 Hz, le plein écran passe à 120 Hz, ou à défaut 60 Hz, en plein écran exclusif, pour que le jeu s'y cale. L'écran revient à sa fréquence en quittant le jeu ou le plein écran (F11). À 60, 120 ou 240 Hz, rien ne change.
- **Écran à fréquence variable (G-Sync, FreeSync)**, désactivé par défaut : sans synchronisation verticale, chaque image est montrée dès qu'elle est prête, à la cadence exacte de la console, et l'écran la suit. N'activez ce réglage qu'avec un écran G-Sync ou FreeSync et la fréquence variable activée dans le pilote de la carte graphique (sous Windows 11, aussi pour les jeux en fenêtre) : sans elle, l'image se déchire.
- **Réduire la latence**, désactivé par défaut : quand le jeu suit l'écran, il attend après chaque rafraîchissement le plus tard possible pour lire la manette et calculer son image, et un appui se voit plus tôt, de plusieurs millisecondes. Le délai dépend du temps que prend une image sur l'ordinateur ; il diminue de lui-même si une image manque son rafraîchissement. Sans effet en fréquence variable.
- **Moteur graphique** : **Automatique** prend Direct3D 11 sous Windows, où SDL 2 prendrait sinon Direct3D 9, dont l'image passe par une copie du bureau. Avec Direct3D 11, RavenEmu ne garde qu'une image d'avance dans le pilote au lieu de trois, soit jusqu'à deux rafraîchissements de moins entre un appui et l'image. Les autres choix (Direct3D 9, Direct3D 12, OpenGL, Metal, Logiciel) dépendent du système. Si l'image ne s'affiche plus, revenez à **Automatique** ; un moteur qui échoue est de toute façon remplacé par un autre. Pris en compte au prochain démarrage de RavenEmu.

Les autres réglages de cette carte sont pris en compte au lancement du jeu.

Tant que vous maintenez **Espace**, ou la gâchette droite de la manette (RT, R2 ou ZR) si aucun bouton de la console ne l'utilise, le jeu avance quatre fois plus vite, son compris. L'avance rapide est désactivée pendant que le câble sans fil est relié.

Tant que vous maintenez **R**, ou la gâchette gauche (LT, L2 ou ZL) si aucun bouton de la console ne l'utilise, le jeu revient en arrière, image par image, sans le son. RavenEmu garde pour cela les derniers moments de jeu, 64 Mo au plus : de trente secondes d'action à plusieurs minutes de jeu calme. Le retour arrière est impossible en Hardcore et pendant que le câble sans fil est relié ; **Retour arrière**, dans **Réglages > Commandes**, le désactive.

Quand vous passez à une autre fenêtre, le jeu se met en pause (« Pause · la fenêtre est en arrière-plan ») et reprend à votre retour. Une pause faite avec P, elle, attend P. **Pause en arrière-plan**, dans **Réglages > Affichage**, désactive ce comportement. En Hardcore, RetroAchievements peut refuser une pause trop proche de la précédente : le jeu continue alors en arrière-plan.

## Console émulée

**Réglages > Console** choisit la console qui fait tourner les jeux, comme les cœurs Libretro de RavenCore :

- **Jeux Game Boy en couleurs** : les jeux Game Boy tournent sur une Game Boy Color. Ils prennent les couleurs que sa boot ROM leur choisit (Pokémon Rouge en rouge, Bleu en bleu...), ou celles d'une combinaison de touches tenue pendant son logo : brun, rouge, sépia, bleu, gris, pastel, négatif...
- **Game Boy Color d'une Game Boy Advance** : des jeux comme Zelda Oracle ou Shantae débloquent alors leurs bonus.
- **BIOS** de la Game Boy Advance : celui de RavenCore par défaut, ou le vrai, avec ou sans son animation de démarrage. Placez `gba_bios.bin` (16 Kio, le BIOS d'une console) dans le dossier `BIOS`, à côté de `Sauvegardes` ; aucun BIOS n'est fourni. S'il manque, un message le dit et le jeu démarre avec le BIOS de RavenCore.

Ces réglages sont pris en compte au lancement du jeu suivant. Un état ne se recharge que dans le mode qui l'a créé : même console, même BIOS. Les succès RetroAchievements restent ceux du jeu, quelle que soit la console.

## Vibrations et capteurs

Certaines cartouches avaient un moteur ou des capteurs. RavenEmu les fait passer par la manette, réglables dans **Réglages > Commandes**, carte **Vibrations et capteurs** :

- **Vibrations** : la manette vibre avec le moteur de la cartouche (Pokémon Pinball, Drill Dozer, WarioWare: Twisted!...). **Force des vibrations** la règle de 10 à 100 %.
- **Capteurs de mouvement de la manette** : Kirby Tilt 'n' Tumble, Yoshi Topsy-Turvy et WarioWare: Twisted! lisent l'accéléromètre et le gyroscope de la manette, si elle en a (DualShock 4, DualSense, Switch Pro, Joy-Con). La position de la manette au lancement du jeu, et à chaque sortie du menu ou de la pause, compte comme à plat : pour recentrer, ouvrez puis fermez le menu en tenant la manette comme vous jouez. Le stick gauche incline et tourne aussi, avec ou sans capteurs : poussé à fond, il vaut une inclinaison de 1 g, ou un demi-tour par seconde.
- **Soleil sur le capteur solaire (Boktai)** : la part du plein soleil que reçoit la cartouche, 50 % par défaut.

La carte **Stick et turbo** règle le stick gauche :

- **Stick gauche comme croix directionnelle**, activé par défaut : le stick dirige aussi, dans les huit directions, chacune sur un huitième de tour (les diagonales appuient sur deux directions). Dans les jeux à capteurs de mouvement, il garde son rôle : il incline et tourne la cartouche.
- **Zone morte du stick** : la part de sa course qui ne compte pas, 30 % par défaut. Plus elle est grande, plus il faut pousser le stick ; augmentez-la si le personnage bouge tout seul. Le stick relâche sa direction un peu en deçà, pour ne pas hésiter sur la limite.

Toutes les manettes branchées jouent, même ajoutées en cours de partie. Celle dont un bouton a servi en dernier vibre, sert aux capteurs de mouvement, au stick et aux gâchettes, et nomme les boutons à l'écran ; ce que tenait la précédente est relâché.

Ces réglages sont pris en compte au lancement du jeu suivant.

## Câble link sans fil

Deux jeux se relient comme avec un câble link, sans câble : avec RavenEmu Android sur un téléphone ou avec un autre ordinateur, sur le même réseau Wi-Fi ou local.

1. Lancez un jeu sur chaque appareil : le même, ou deux jeux faits pour se relier (Pokémon Rouge et Bleu, par exemple). Game Boy et Game Boy Color se relient entre eux ; un Game Boy Advance se relie à un autre Game Boy Advance.
2. Dans le jeu, allez là où il se sert du câble (salle d'échange, combat à deux...). RavenEmu cherche alors un autre appareil et branche le câble tout seul : « Câble relié à... » s'affiche.
3. Si plusieurs appareils sont prêts, **Relier le câble à...** vous laisse choisir, au clavier, à la souris ou à la manette ; **Plus tard** les écarte pour cette partie. Cet ordinateur apparaît chez les autres sous le nom de son système (PC Windows, Mac ou PC Linux) suivi d'un code de quatre caractères.

Pendant la liaison :

- Le menu de pause d'un côté met aussi l'autre jeu en attente (« Partenaire en pause »), comme deux consoles reliées.
- Le chargement d'un état est désactivé : il séparerait les deux parties. Sauvegarder un état reste possible.
- **Débrancher le câble**, dans le menu de pause, sépare les deux jeux ; ils ne se relient plus d'eux-mêmes pendant cette partie. Quitter le jeu débranche aussi le câble.
- Au Game Boy Advance, l'un des deux appareils tient le rôle du joueur 1 (le parent), toujours le même pour une paire donnée.

Les deux appareils doivent avoir une version récente de RavenEmu, avec le même câble : un appareil resté sur une version plus ancienne ne se relie pas. Mettez à jour RavenEmu sur chacun.

Chaque échange de données attend un aller-retour réseau : pendant que le câble sert, les deux jeux ralentissent ensemble, d'autant plus que le réseau est lent. Sur un échange Pokémon Rubis ↔ Émeraude, ils passent d'environ 60 images par seconde sans latence à 16 avec 6 ms d'aller-retour, et 5 avec 20 ms ; l'échange va au bout. Pour un échange plus fluide, préférez un câble Ethernet ou le Wi-Fi 5 GHz près de la box, et coupez un éventuel VPN, qui peut ralentir ou bloquer le trafic local.

Le réglage **Câble link sans fil**, dans **Réglages > Câble link**, est activé par défaut ; désactivé, RavenEmu ne s'annonce jamais sur le réseau. Au premier usage, le pare-feu de Windows, ou macOS (accès au réseau local), peut demander une autorisation : acceptez-la pour les réseaux privés. Sous Linux, un pare-feu actif doit laisser passer le port UDP 5353 (mDNS) et les connexions TCP entrantes de RavenEmu sur le réseau local. Les réseaux invités ou publics qui isolent les appareils empêchent la découverte.

## Jouer à deux sur un même PC

**Jouer à deux**, sous **Jouer** dans le panneau d'un jeu de la bibliothèque, relie ce jeu à un second par un câble link virtuel, côte à côte dans une même fenêtre : un échange ou un combat Pokémon, un duel de Tetris... Choisissez le jeu du joueur 2 parmi ceux qui se relient au premier : Game Boy et Game Boy Color entre eux, ou deux Game Boy Advance. Les deux jeux doivent être deux fichiers différents, puisque chacun garde sa propre sauvegarde, la même que lorsqu'il est joué seul.

- Le joueur 1 joue à gauche, le joueur 2 à droite.
- Le clavier joue le joueur 1. Avec une seule manette, elle joue le joueur 2 ; avec deux, la première branchée joue le joueur 1 et la seconde le joueur 2.
- **Échanger les joueurs**, dans le menu de pause, inverse tout : pratique pour jouer seul aux deux jeux, par exemple pour s'échanger des Pokémon entre deux versions. Le menu rappelle qui joue quoi.
- Un seul jeu s'entend, celui du joueur 1 au départ ; **Entendre le jeu du joueur 2** passe à l'autre.
- Échap, ou Select + Start à l'une des manettes, ouvre le menu de pause, qui arrête les deux jeux. **Réinitialiser les deux consoles** les redémarre ensemble.

Les états instantanés, le retour arrière, l'avance rapide, les codes de triche, les captures d'écran et RetroAchievements restent réservés aux jeux joués seuls. Au Game Boy Advance, le joueur 1 tient le rôle du parent.

## États instantanés

Chaque jeu a neuf emplacements d'états. **F5** sauvegarde dans l'emplacement choisi et **F9** le recharge ; **F6** et **F7** passent à l'emplacement précédent ou suivant, et une notification indique s'il est vide ou quand il a été sauvegardé. Dans le menu de pause, les lignes **Sauvegarder l'état** et **Charger l'état** indiquent l'emplacement : les flèches gauche et droite (ou la croix de la manette, ou la molette sur la ligne) en changent. Une vignette montre alors, à côté du menu, l'image du jeu au moment de la sauvegarde et sa date.

L'emplacement 1 reprend l'état unique des versions précédentes. Après un chargement, **Annuler le chargement**, dans le menu de pause, ramène la partie où elle en était juste avant ; une seule fois.

## Codes de triche

**Codes de triche**, dans le menu de pause, liste les codes du jeu. Chacun s'active, se désactive ou se supprime ; tout changement s'applique dès l'image suivante et reste mémorisé pour la prochaine partie, dans un fichier `.cheats` à côté des sauvegardes du jeu.

Pour ajouter un code, donnez-lui un nom (facultatif), collez ou tapez le code sur une ou plusieurs lignes, puis **Ajouter** :

- Game Boy et Game Boy Color : Game Genie (`ABC-DEF-GHI`) ou GameShark (`01xxxxxx`).
- Game Boy Advance : Codebreaker non chiffré, GameShark ou Action Replay chiffrés, tels que l'appareil les affiche.

RavenCore vérifie le code avant de l'accepter ; un code refusé affiche la raison, par exemple un patch de ROM, non pris en charge. Les codes sont désactivés en Hardcore et reprennent en Softcore. En mode TV, faute de clavier, le menu n'a pas cette entrée.

## Captures d'écran

**F8**, ou **Capture d'écran** dans le menu de pause, enregistre la dernière image du jeu, sans le menu ni les notifications, à la taille de la console (160 × 144 ou 240 × 160 pixels). Le fichier PNG va dans le dossier `Captures`, à côté de `Sauvegardes`, sous le nom du jeu suivi de la date et de l'heure. **Réglages > Bibliothèque** l'ouvre. En mode TV, le menu n'a pas cette entrée, faute de place : F8 reste disponible au clavier.

## Sauvegardes et jaquettes

Sous Windows et Linux, l'application utilise les dossiers `Sauvegardes` et `Jaquettes` à côté de l'exécutable. Si cet emplacement n'est pas accessible en écriture, elle essaie son dossier de données utilisateur. L'application `.app` macOS place ces dossiers dans les données utilisateur pour préserver sa signature ; une compilation macOS en exécutable simple garde le comportement portable. Les chemins utilisés sont affichés dans **Réglages**.

La bibliothèque et les réglages sont enregistrés séparément dans le dossier de données utilisateur `RavenEmu` : `%APPDATA%\RavenEmu` sous Windows, `~/.local/share/RavenEmu` sous Linux et `~/Library/Application Support/RavenEmu` sous macOS. Copier une ROM ne transfère donc pas automatiquement sa progression : conservez également vos sauvegardes.

Les versions qui s'appelaient RavenEmuDSK rangeaient ces données dans des dossiers `RavenEmuDSK`. Au premier lancement, RavenEmu les déplace dans le dossier `RavenEmu`, sans jamais remplacer un fichier qui s'y trouve déjà ; un fichier en double reste à son ancienne place.

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
