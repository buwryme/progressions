# Progressions

## an arch linux system upgrader/maintenance tool
by default, it runs 4 commands:
```
yay -Syuu
mkinitcpio -p
flatpak upgrade
```
and other system refreshal commands, such as dns flushing, etc...

## to compile, run:
`make`

## to clean artifacts:
`make clean`

## to run the compiled binary:
`./progressions`

## to install progressions:
`sudo make install`

## to uninstall progressions:
`sudo make uninstall`
