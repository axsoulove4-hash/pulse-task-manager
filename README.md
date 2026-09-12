# Pulse

Gestionnaire de tâches Linux natif, personnalisable, avec processus en arbre, logos, contrôle des signaux et interface bilingue.

Native, customizable Linux task manager with process trees, application icons, signal controls and a bilingual interface.

## Fonctionnalités / Features

- **Français / English** : choisissez la langue dans Réglages / choose the language in Settings (redémarrage requis / restart required).
- Onglets Vue générale, Processeur (graphique par cœur), Cartes graphiques (un onglet par carte) et Températures / Overview, CPU (one chart per core), Graphics cards (one tab per card) and Temperatures tabs.
- Lecture GPU et sondes thermiques via `/sys` quand le pilote les expose / GPU telemetry and thermal sensors through `/sys` when exposed by the driver.
- Vue performances CPU, mémoire, réseau, disques et swap / CPU, memory, network, disk and swap charts.
- Processus classés par catégories et sous-processus repliables / categorized processes with collapsible children.
- Logos détectés depuis les lanceurs `.desktop`, Flatpak et thèmes d’icônes / icons detected from `.desktop`, Flatpak and icon themes.
- Clic droit : terminer, forcer, suspendre, reprendre, détails / right-click actions: end, force stop, suspend, resume, details.
- Priorités Linux : très basse, basse, normale, haute, très haute / very low, low, normal, high, very high.
- Export CSV de la liste / CSV process export.
- Thèmes Minuit, Ardoise, Clair, accent libre et densité compacte / Midnight, Slate, Light themes, custom accent and compact density.
- AppImage portable et compilation native / portable AppImage and native build.

## Installer / Install

Dépendances / Dependencies: CMake, C++17, Qt6 Widgets et Qt6 Svg.

```sh
./install.sh
```

Installation utilisateur / User install: `~/.local/bin/pulse`.

## Tester / Test

```sh
QT_QPA_PLATFORM=offscreen ./build/pulse --self-test
QT_QPA_PLATFORM=offscreen ./build/pulse --ui-test
```

## Compiler portable / Portable build

Voir / See [`packaging/README-portable.md`](packaging/README-portable.md). Scripts pour / scripts for AppImage, Podman/Docker et tests / tests.

## Personnaliser / Customize

Modifier [`style.qss`](style.qss), puis relancer / edit it, then run `./install.sh`. Les réglages sont sauvegardés dans / settings are stored in `~/.config/Pulse/TaskManager.conf`.

## Limites / Limitations

Les actions respectent les droits Linux de l’utilisateur courant / actions respect current Linux permissions. Certaines distributions, architectures ARM, musl ou bureaux peuvent demander une compilation native / some distributions, ARM architectures, musl systems or desktops may require a native build.

## Licence / License

MIT — voir / see [`LICENSE`](LICENSE).
