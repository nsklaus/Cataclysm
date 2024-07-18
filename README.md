
# Cataclysm:
This is a roguelike set in a post-apocalyptic world. Struggle to survive in a harsh, persistent, procedurally generated world. Scavenge the remnants of a dead civilization for food or equipment. Fight to defeat or escape from a wide variety of powerful monstrosities and against the others like yourself, that want what you have...

### Compile
for linux: install dependencies: `SDL2-devel SDL2_mixer-devel SDL2_ttf-devel SDL2_image-devel gcc-libs, glibc, zlib, bzip2 ncurses freetype2`
then just do `make all` or `make -j$(nproc) all` (see the Makefile).

### yet another fork ?
yes, using the older branch, version 0E from 2020, i want to experiment with the UI, the tiles and world system, and check as well if i can make it also loose a lot of bloat while experimenting on various elements of the game. this is primarily a pet project for me to play with the code. there is no will to make it something usable for everyone. 
