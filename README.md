# Progressions

## a GNU/Linux system upgrader/maintenance tool
by default, it runs 4 commands that fit Arch Linux/derivatives better:
```
yay -Syuu
mkinitcpio -p
flatpak upgrade
```
and other system refreshal commands, such as dns flushing, cache clearing, etc...
but you can change those in settings to fit your distribution better

### Progressions uses:
- libayatana-appindicator for tray item integration
- gtk4 for it's interface
- toml++ for configuration

**so to compile, you'll need to install those:**
`sudo pacman -S gtkmm4 libadwaita libnotify libayatana-appindicator tomlplusplus`

## makefile commands:
### to compile, run:
`make`

### to clean artifacts:
`make clean`

### to run the compiled binary:
`./progressions`

### to install progressions:
`sudo make install`

### to uninstall progressions:
`sudo make uninstall`
