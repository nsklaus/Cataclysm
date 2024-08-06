# Compiler and Linker
CXX := clang++
CC := clang++

# Directories
OBJDIR := obj
SRCDIR := src
INCDIR := include
DATADIR := data

RELEASE_FLAGS = -DRELEASE -DTILES -DBACKTRACE -DLOCALIZE -ffast-math -Os -Wall -Wextra -Wformat-signedness -Wlogical-op -Wmissing-declarations -Wmissing-noreturn -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpedantic -Wsuggest-override -Wunused-macros -Wzero-as-null-pointer-constant -Wno-unknown-warning-option -Wno-deprecated -Wredundant-decls -fsigned-char -std=c++14 -MMD -MP -m64 -I/usr/include/SDL2 -D_REENTRANT -DSDL_SOUND -I/usr/include/SDL2 -D_REENTRANT -I/usr/include/SDL2 -D_REENTRANT -DHWY_SHARED_DEFINE -DWITH_GZFILEOP -DAVIF_DLL -I/usr/include/webp -I/usr/include/SDL2 -D_REENTRANT -DWITH_GZFILEOP -I/usr/include/libpng16 -I/usr/include/harfbuzz -I/usr/include/freetype2 -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/sysprof-6 -pthread
DEBUG_FLAGS = -g $(RELEASE_FLAGS)
LDFLAGS = -lSDL2 -lSDL2_image -lSDL2_mixer -lSDL2_ttf -lwebp -lpng -lfreetype -lharfbuzz -lglib-2.0 -lgobject-2.0 -lgthread-2.0 -lm

# Platform-specific setup
PLATFORM := LINUX
OPTFLAGS := -O2
DEBUGFLAGS := -g

# Combine flags
CXXFLAGS += $(DEBUGFLAGS) $(WARNINGS) $(EXTRAFLAGS) $(OPTFLAGS)

# Source Files
SOURCES := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# Include Dependencies
-include $(OBJECTS:.o=.d)

# Targets and rules
all: CXXFLAGS = $(RELEASE_FLAGS)
all: cataclysm

debug: CXXFLAGS = $(DEBUG_FLAGS)
debug: cataclysm

cataclysm: $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJDIR)/*.o $(OBJDIR)/*.d cataclysm

.PHONY: all clean

