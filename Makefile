# Basic settings
CC = g++
CXXFLAGS = -DRELEASE -DGIT_VERSION -DTILES -DLOCALIZE -ffast-math -Wall -Wextra -fsigned-char -Wno-deprecated -std=c++11 -MMD -MP -m64 -I/usr/include/SDL2 -D_REENTRANT -DSDL_SOUND -I/usr/include/SDL2 -D_REENTRANT -I/usr/include/SDL2 -D_REENTRANT -DHWY_SHARED_DEFINE -DWITH_GZFILEOP -DAVIF_DLL -I/usr/include/webp -I/usr/include/SDL2 -D_REENTRANT -DWITH_GZFILEOP -I/usr/include/libpng16 -I/usr/include/harfbuzz -I/usr/include/freetype2 -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/sysprof-6 -pthread -DEXCLUDE_OPTIONAL_SWAP
LDFLAGS = -lSDL2 -lSDL2_image -lSDL2_mixer -lSDL2_ttf -lwebp -lpng -lfreetype -lharfbuzz -lglib-2.0 -lgobject-2.0 -lgthread-2.0

# Files and directories
SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

# Targets and rules
all: cataclysm-tiles

cataclysm-tiles: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d cataclysm-tiles
