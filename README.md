# Pulse

Gestionnaire de tâches natif Linux. C++17 / Qt 6 Widgets. Logo SVG original intégré au binaire.

## Utilisation

Lancer **Pulse** depuis le menu d’applications, ou `~/.local/bin/pulse`.

- Vue d’ensemble : CPU, RAM, réseau et disques, courbes de 90 mesures, swap.
- Processus : recherche nom/PID/utilisateur/commande, tri numérique, filtre personnel, détails.
- Contrôle : terminer, arrêt forcé, suspendre, reprendre, priorité Linux. Les actions utilisent les droits de l’utilisateur courant. Aucun sudo automatique.
- Personnalisation : Minuit, Ardoise, Clair, accent libre, fréquence 0,5–5 s, densité, colonnes visibles, premier plan.
- Ctrl+F : recherche. Espace : figer/reprendre les mesures.
- Réglages persistants : `~/.config/Pulse/TaskManager.conf` (premier plan non persistant).

## Compiler / installer

Dépendances : CMake, compilateur C++17, bibliothèques de développement Qt6 Widgets et Svg.

```sh
./install.sh
```

Installation utilisateur dans `~/.local/bin`, `~/.local/share/applications` et `~/.local/share/icons`. Pas de droits administrateur nécessaires. Binaire lié aux bibliothèques Qt système, pas un AppImage autonome.

## Vérifier

```sh
QT_QPA_PLATFORM=offscreen ./build/pulse --self-test
QT_QPA_PLATFORM=offscreen ./build/pulse --ui-test
QT_QPA_PLATFORM=offscreen ./build/pulse --screenshot
```

Premier test : lecture procfs et gestion d’un processus sleep créé pour le test (pause, reprise, priorité, terminaison). Second : navigation, recherche vide, recherche rétablie, tri mémoire numérique et captures des pages.

## Interprétation et limites

CPU processus = part de tous les processeurs logiques, de 0 à 100 %. Première mesure CPU/débits = initialisation. Mémoire processus = RSS ; pages partagées peuvent apparaître dans plusieurs processus. Données proviennent de `/proc` et `/sys`, certaines commandes peuvent être masquées par permissions système. Réseau = somme des interfaces hors loopback, les interfaces virtuelles peuvent compter plusieurs fois un trafic. Disques = compteurs périphériques hors loop/ram/dm/md ; débits, pas occupation de stockage. Pas de mesure GPU, température, services ou applications au démarrage dans cette version.

Envoi de signaux via pidfd, avec vérification de l’identité du processus après ouverture. Priorité : vérification préalable du temps de démarrage, puis API Linux setpriority par PID.

## Désinstaller

Supprimer les fichiers `~/.local/bin/pulse`, `~/.local/share/applications/pulse.desktop`, `~/.local/share/icons/hicolor/scalable/apps/pulse.svg`. Sources et réglages peuvent être conservés.

## Interface bureau

Onglets Performances / Processus / Réglages, avec ouverture sur Processus. Style sobre défini dans `style.qss` et intégré au binaire via les ressources Qt. Pour changer les espacements, bordures ou typographies, modifier ce fichier puis lancer `./install.sh`. Les tokens `@bg`, `@text`, `@accent`, etc. sont remplacés par la palette choisie dans Réglages. QSS est le langage de styles Qt, proche de CSS.

## Logos, clic droit et priorités

Les logos sont associés aux processus à partir des lanceurs `.desktop` (y compris Flatpak), du nom de l’exécutable et du thème d’icônes. Un processus sans logo connu reçoit une icône générique. L’index est chargé au démarrage et les correspondances sont mises en cache.

Clic droit sur une ligne : terminer, forcer l’arrêt, suspendre, reprendre, sous-menu Priorité et détails. Les mesures sont temporairement figées pendant le menu pour préserver la ligne choisie. Les confirmations d’arrêt restent actives.

Priorités : Très basse = 19, Basse = 10, Normale = 0, Haute = −5, Très haute = −10. Les valeurs Linux intermédiaires sont regroupées sous un libellé ; la valeur exacte reste visible dans Détails. Linux peut refuser une hausse avec les droits actuels, y compris un retour à Normale après avoir abaissé la priorité. Aucun changement global des permissions.

## Catégories et sous-processus

La page Processus regroupe les lignes en Applications, Services système, Mes processus et Autres. Chaque parent possède une flèche : elle déplie ou replie ses enfants issus de la relation Linux parent/PID. Replier une ligne ne tue rien. Un clic droit sur une catégorie propose « Terminer tout le groupe » et « Forcer l’arrêt de tout le groupe » ; le signal est envoyé à chaque descendant via `pidfd`, avec confirmation et compteur succès/échecs.

## Distribution Linux portable

Voir `packaging/README-portable.md` pour l’AppImage x86-64, les limites de compatibilité, les dépendances multi-distributions et la compilation native sur d’autres architectures. `packaging/build-appimage.sh` compile sur Ubuntu 22.04 dans Podman/Docker. `packaging/test-portable.sh` teste le résultat sans FUSE. Les archives publiables sont dans `dist/`.
