
# Cataclysm:

This is a roguelike set in a post-apocalyptic world. Struggle to survive in a harsh, persistent, procedurally generated world. Scavenge the remnants of a dead civilization for food or equipment. Fight to defeat or escape from a wide variety of powerful monstrosities and against the others like yourself, that want what you have...

## Compile
for linux: install dependencies: `SDL2-devel SDL2_mixer-devel SDL2_ttf-devel SDL2_image-devel gcc-libs, glibc, zlib, bzip2 ncurses freetype2`
then just do `make` (see the Makefile).

## Frequently Asked Questions

#### How can I change the key bindings?

Press the `?` key, followed by the `1` key to see the full list of key commands. Press the `+` key to add a key binding, select which action with the corresponding letter key `a-w`, and then the key you wish to assign to that action.

#### How can I start a new world?

**World** on the main menu will generate a fresh world for you. Select **Create World**.

#### yet another fork ?
yes, using the much older version 0D from 2019, i want to experiment with the UI, the tiles and world system, and check as well if i can make it also loose a lot of bloat while experimenting on various elements of the game. cloning this repo is around 100mb whereas cloning the current upstream master is 9gb. the Makefile is 30lines whereas upstream one is over a 1000lines. final executable is 40mb vs 240mb.. and so on.. this is primarily a pet project for me to play with the code. there is no will to make something usable for everyone. it is basicaly a playfield for me, that i will toy with and it will probably end up rotting on this repo once i'm done with it. 
