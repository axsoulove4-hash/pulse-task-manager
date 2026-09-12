# Pulse 1.1 — Linux portable

## AppImage x86-64

```sh
chmod +x Pulse-1.1-x86_64.AppImage
./Pulse-1.1-x86_64.AppImage
```

Si FUSE n’est pas disponible :

```sh
./Pulse-1.1-x86_64.AppImage --appimage-extract-and-run
```

Qt est inclus. Configuration conservée dans `~/.config/Pulse/TaskManager.conf`. Base Ubuntu 22.04, Qt 6.2.4, glibc 2.35, x86-64. Distributions glibc récentes (Ubuntu 22.04+, Debian 12+, Fedora/Nobara, Arch/Manjaro, openSUSE) ciblées. Alpine/musl, ARM, NixOS et systèmes anciens peuvent nécessiter compilation native.

## Compilation native

```sh
./packaging/dependencies.sh
./install.sh --install-deps
```

Dépendances : Linux /proc /sys, C++17, CMake 3.20+, Qt6 Widgets/Svg >= 6.2. Recettes apt, dnf, pacman, zypper, apk, xbps.

## AppImage

`./packaging/build-appimage.sh` nécessite Podman ou Docker, réseau et x86-64.
