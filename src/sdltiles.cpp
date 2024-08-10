
#include "sdltiles.h" // IWYU pragma: associated
#include "cursesdef.h" // IWYU pragma: associated

#include <cstdint>
#include <climits>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#include <array>
#include <cmath>
#include <exception>
#include <iterator>
#include <map>
#include <set>
#include <type_traits>
#include <tuple>

#include "platform_win.h"

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_syswm.h>


#include "avatar.h"
#include "cata_tiles.h"
#include "catacharset.h"
#include "color.h"
#include "color_loader.h"
#include "cursesport.h"
#include "debug.h"
#include "filesystem.h"
#include "font_loader.h"
#include "game.h"
#include "game_ui.h"
#include "get_version.h"
#include "input.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "sdlsound.h"
#include "sdl_wrappers.h"
#include "string_formatter.h"
#include "translations.h"
#include "wcwidth.h"
#include "json.h"
#include "optional.h"
#include "point.h"


#include <cstdlib> // getenv()/setenv()
#include <strings.h> // for strcasecmp



#define dbg(x) DebugLog((x),D_SDL) << __FILE__ << ":" << __LINE__ << ": "

//***********************************
//Globals                           *
//***********************************

std::unique_ptr<cata_tiles> tilecontext;
static uint32_t lastupdate = 0;
static uint32_t interval = 25;
static bool needupdate = false;

// used to replace SDL_RenderFillRect with a more efficient SDL_RenderCopy
SDL_Texture_Ptr alt_rect_tex = nullptr;
bool alt_rect_tex_enabled = false;

std::array<SDL_Color, color_loader<SDL_Color>::COLOR_NAMES_COUNT> windowsPalette;

/**
 * A class that draws a single character on screen.
 */
class CataFont
{
    public:
        CataFont( int w, int h ) :
            fontwidth( w ), fontheight( h ) { }
        virtual ~CataFont() = default;

        virtual bool isGlyphProvided( const std::string &ch ) const = 0;
        /**
         * Draw character t at (x,y) on the screen,
         * using (curses) color.
         */
        virtual void OutputChar( const std::string &ch, int x, int y,
                                 unsigned char color, float opacity = 1.0f ) = 0;
        virtual void draw_ascii_lines( unsigned char line_id, int drawx, int drawy, int FG ) const;
        bool draw_window( const catacurses::window &w );
        bool draw_window( const catacurses::window &w, int offsetx, int offsety );

        static std::unique_ptr<CataFont> load_font( const std::string &typeface, int fontsize, int fontwidth,
                                                int fontheight, bool fontblending );
    public:
        // the width of the font, background is always this size
        int fontwidth;
        // the height of the font, background is always this size
        int fontheight;
};

/**
 * Uses a ttf font. Its glyphs are cached.
 */
class CachedTTFFont : public CataFont
{
    public:
        CachedTTFFont( int w, int h, std::string typeface, int fontsize, bool fontblending );
        ~CachedTTFFont() override = default;

        bool isGlyphProvided( const std::string &ch ) const override;
        void OutputChar( const std::string &ch, int x, int y,
                         unsigned char color, float opacity = 1.0f ) override;
    protected:
        SDL_Texture_Ptr create_glyph( const std::string &ch, int color );

        TTF_Font_Ptr font;
        // Maps (character code, color) to SDL_Texture*

        struct key_t {
            std::string   codepoints;
            unsigned char color;

            // Operator overload required to use in std::map.
            bool operator<( const key_t &rhs ) const noexcept {
                return ( color == rhs.color ) ? codepoints < rhs.codepoints : color < rhs.color;
            }
        };

        struct cached_t {
            SDL_Texture_Ptr texture;
            int          width;
        };

        std::map<key_t, cached_t> glyph_cache_map;

        const bool fontblending;
};

/**
 * A font created from a bitmap. Each character is taken from a
 * specific area of the source bitmap.
 */
class BitmapFont : public CataFont
{
    public:
        BitmapFont( int w, int h, const std::string &typeface_path );
        ~BitmapFont() override = default;

        bool isGlyphProvided( const std::string &ch ) const override;
        void OutputChar( const std::string &ch, int x, int y,
                         unsigned char color, float opacity = 1.0f ) override;
        void OutputChar( int t, int x, int y,
                         unsigned char color, float opacity = 1.0f );
        void draw_ascii_lines( unsigned char line_id, int drawx, int drawy, int FG ) const override;
    protected:
        std::array<SDL_Texture_Ptr, color_loader<SDL_Color>::COLOR_NAMES_COUNT> ascii;
        int tilewidth;
};

class FontFallbackList : public CataFont
{
    public:
        FontFallbackList( int w, int h, const std::vector<std::string> &typefaces,
                          int fontsize, bool fontblending );
        ~FontFallbackList() override = default;

        bool isGlyphProvided( const std::string &ch ) const override;
        void OutputChar( const std::string &ch, int x, int y,
                         unsigned char color, float opacity = 1.0f ) override;
    protected:
        std::vector<std::unique_ptr<CataFont>> fonts;
        std::map<std::string, std::vector<std::unique_ptr<CataFont>>::iterator> glyph_font;
};

static std::unique_ptr<FontFallbackList> font;
static std::unique_ptr<FontFallbackList> map_font;
static std::unique_ptr<FontFallbackList> overmap_font;

static SDL_Window_Ptr window;
static SDL_Renderer_Ptr renderer;
static SDL_PixelFormat_Ptr format;
static SDL_Texture_Ptr display_buffer;

static int WindowWidth;        //Width of the actual window, not the curses window
static int WindowHeight;       //Height of the actual window, not the curses window
// input from various input sources. Each input source sets the type and
// the actual input value (key pressed, mouse button clicked, ...)
// This value is finally returned by input_manager::get_input_event.
static input_event last_input;

static constexpr int ERR = -1;
static int inputdelay;         //How long getch will wait for a character to be typed
static Uint32 delaydpad =
    std::numeric_limits<Uint32>::max();     // Used for entering diagonal directions with d-pad.
static Uint32 dpad_delay =
    100;   // Delay in milliseconds between registering a d-pad event and processing it.
static bool dpad_continuous = false;  // Whether we're currently moving continuously with the dpad.
static int lastdpad = ERR;      // Keeps track of the last dpad press.
static int queued_dpad = ERR;   // Queued dpad press, for individual button presses.
int fontwidth;          //the width of the font, background is always this size
int fontheight;         //the height of the font, background is always this size
static int TERMINAL_WIDTH;
static int TERMINAL_HEIGHT;
bool fullscreen;
int scaling_factor;

static SDL_Joystick *joystick; // Only one joystick for now.

using cata_cursesport::curseline;
using cata_cursesport::cursecell;
static std::vector<curseline> oversized_framebuffer;
static std::vector<curseline> terminal_framebuffer;
static std::weak_ptr<void> winBuffer; //tracking last drawn window to fix the framebuffer
static int fontScaleBuffer; //tracking zoom levels to fix framebuffer w/tiles
extern catacurses::window w_hit_animation; //this window overlays w_terrain which can be oversized

//***********************************
//Non-curses, Window functions      *
//***********************************
static void generate_alt_rect_texture()
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    static const Uint32 rmask = 0xff000000;
    static const Uint32 gmask = 0x00ff0000;
    static const Uint32 bmask = 0x0000ff00;
    static const Uint32 amask = 0x000000ff;
#else
    static const Uint32 rmask = 0x000000ff;
    static const Uint32 gmask = 0x0000ff00;
    static const Uint32 bmask = 0x00ff0000;
    static const Uint32 amask = 0xff000000;
#endif

    SDL_Surface_Ptr alt_surf( SDL_CreateRGBSurface( 0, 1, 1, 32, rmask, gmask, bmask, amask ) );
    if( alt_surf ) {
        FillRect( alt_surf, nullptr, SDL_MapRGB( alt_surf->format, 255, 255, 255 ) );

        alt_rect_tex.reset( SDL_CreateTextureFromSurface( renderer.get(), alt_surf.get() ) );
        alt_surf.reset();

        // Test to make sure color modulation is supported by renderer
        alt_rect_tex_enabled = !SetTextureColorMod( alt_rect_tex, 0, 0, 0 );
        if( !alt_rect_tex_enabled ) {
            alt_rect_tex.reset();
        }
        DebugLog( D_INFO, DC_ALL ) << "generate_alt_rect_texture() = " << ( alt_rect_tex_enabled ? "FAIL" :
                                   "SUCCESS" ) << ". alt_rect_tex_enabled = " << alt_rect_tex_enabled;
    } else {
        DebugLog( D_ERROR, DC_ALL ) << "CreateRGBSurface failed: " << SDL_GetError();
    }
}

void draw_alt_rect( const SDL_Renderer_Ptr &renderer, const SDL_Rect &rect,
                    Uint32 r, Uint32 g, Uint32 b )
{
    SetTextureColorMod( alt_rect_tex, r, g, b );
    RenderCopy( renderer, alt_rect_tex, nullptr, &rect );
}

static bool operator==( const cata_cursesport::WINDOW *const lhs, const catacurses::window &rhs )
{
    return lhs == rhs.get();
}

static void ClearScreen()
{
    SetRenderDrawColor( renderer, 0, 0, 0, 255 );
    RenderClear( renderer );
}

static void InitSDL()
{
    int init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    int ret;

#if defined(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING)
    SDL_SetHint( SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1" );
#endif


    // https://bugzilla.libsdl.org/show_bug.cgi?id=3472#c5
    if( SDL_COMPILEDVERSION == SDL_VERSIONNUM( 2, 0, 5 ) ) {
        const char *xmod = getenv( "XMODIFIERS" );
        if( xmod && strstr( xmod, "@im=ibus" ) != nullptr ) {
            setenv( "XMODIFIERS", "@im=none", 1 );
        }
    }


    ret = SDL_Init( init_flags );
    throwErrorIf( ret != 0, "SDL_Init failed" );

    ret = TTF_Init();
    throwErrorIf( ret != 0, "TTF_Init failed" );

    // cata_tiles won't be able to load the tiles, but the normal SDL
    // code will display fine.
    ret = IMG_Init( IMG_INIT_PNG );
    printErrorIf( ( ret & IMG_INIT_PNG ) != IMG_INIT_PNG,
                  "IMG_Init failed to initialize PNG support, tiles won't work" );

    ret = SDL_InitSubSystem( SDL_INIT_JOYSTICK );
    printErrorIf( ret != 0, "Initializing joystick subsystem failed" );

    //SDL2 has no functionality for INPUT_DELAY, we would have to query it manually, which is expensive
    //SDL2 instead uses the OS's Input Delay.

    atexit( SDL_Quit );
}

static bool SetupRenderTarget()
{
    SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
    display_buffer.reset( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_TARGET, WindowWidth / scaling_factor, WindowHeight / scaling_factor ) );
    if( printErrorIf( !display_buffer, "Failed to create window buffer" ) ) {
        return false;
    }
    if( printErrorIf( SDL_SetRenderTarget( renderer.get(), display_buffer.get() ) != 0,
                      "SDL_SetRenderTarget failed" ) ) {
        return false;
    }
    ClearScreen();

    return true;
}

//Registers, creates, and shows the Window!!
static void WinCreate()
{
    std::string version = string_format( "Cataclysm: Dark Days Ahead - %s", getVersionString() );

    // Common flags used for fulscreen and for windowed
    int window_flags = 0;
    WindowWidth = TERMINAL_WIDTH * fontwidth * scaling_factor;
    WindowHeight = TERMINAL_HEIGHT * fontheight * scaling_factor;
    window_flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    if( get_option<std::string>( "SCALING_MODE" ) != "none" ) {
        SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, get_option<std::string>( "SCALING_MODE" ).c_str() );
    }

    if( get_option<std::string>( "FULLSCREEN" ) == "fullscreen" ) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
        fullscreen = true;
        SDL_SetHint( SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0" );
    } else if( get_option<std::string>( "FULLSCREEN" ) == "windowedbl" ) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        fullscreen = true;
        SDL_SetHint( SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0" );
    } else if( get_option<std::string>( "FULLSCREEN" ) == "maximized" ) {
        window_flags |= SDL_WINDOW_MAXIMIZED;
    }


    int display = std::stoi( get_option<std::string>( "DISPLAY" ) );
    if( display < 0 || display >= SDL_GetNumVideoDisplays() ) {
        display = 0;
    }

    ::window.reset( SDL_CreateWindow( version.c_str(),
                                      SDL_WINDOWPOS_CENTERED_DISPLAY( display ),
                                      SDL_WINDOWPOS_CENTERED_DISPLAY( display ),
                                      WindowWidth,
                                      WindowHeight,
                                      window_flags
                                    ) );
    throwErrorIf( !::window, "SDL_CreateWindow failed" );


    // On Android SDL seems janky in windowed mode so we're fullscreen all the time.
    // Fullscreen mode is now modified so it obeys terminal width/height, rather than
    // overwriting it with this calculation.
    if( window_flags & SDL_WINDOW_FULLSCREEN || window_flags & SDL_WINDOW_FULLSCREEN_DESKTOP
        || window_flags & SDL_WINDOW_MAXIMIZED ) {
        SDL_GetWindowSize( ::window.get(), &WindowWidth, &WindowHeight );
        // Ignore previous values, use the whole window, but nothing more.
        TERMINAL_WIDTH = WindowWidth / fontwidth / scaling_factor;
        TERMINAL_HEIGHT = WindowHeight / fontheight / scaling_factor;
    }

    // Initialize framebuffer caches
    terminal_framebuffer.resize( TERMINAL_HEIGHT );
    for( int i = 0; i < TERMINAL_HEIGHT; i++ ) {
        terminal_framebuffer[i].chars.assign( TERMINAL_WIDTH, cursecell( "" ) );
    }

    oversized_framebuffer.resize( TERMINAL_HEIGHT );
    for( int i = 0; i < TERMINAL_HEIGHT; i++ ) {
        oversized_framebuffer[i].chars.assign( TERMINAL_WIDTH, cursecell( "" ) );
    }

    const Uint32 wformat = SDL_GetWindowPixelFormat( ::window.get() );
    format.reset( SDL_AllocFormat( wformat ) );
    throwErrorIf( !format, "SDL_AllocFormat failed" );

    int renderer_id = -1;

    bool software_renderer = get_option<std::string>( "RENDERER" ).empty();
    std::string renderer_name;
    if( software_renderer ) {
        renderer_name = "software";
    } else {
        renderer_name = get_option<std::string>( "RENDERER" );
    }

    const int numRenderDrivers = SDL_GetNumRenderDrivers();
    for( int i = 0; i < numRenderDrivers; i++ ) {
        SDL_RendererInfo ri;
        SDL_GetRenderDriverInfo( i, &ri );
        if( renderer_name == ri.name ) {
            renderer_id = i;
            DebugLog( D_INFO, DC_ALL ) << "Active renderer: " << renderer_id << "/" << ri.name;
            break;
        }
    }


    if( !software_renderer ) {
        dbg( D_INFO ) << "Attempting to initialize accelerated SDL renderer.";

        renderer.reset( SDL_CreateRenderer( ::window.get(), renderer_id, SDL_RENDERER_ACCELERATED |
                                            SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE ) );
        if( printErrorIf( !renderer,
                          "Failed to initialize accelerated renderer, falling back to software rendering" ) ) {
            software_renderer = true;
        } else if( !SetupRenderTarget() ) {
            dbg( D_ERROR ) <<
                           "Failed to initialize display buffer under accelerated rendering, falling back to software rendering.";
            software_renderer = true;
            display_buffer.reset();
            renderer.reset();
        }
    }

    if( software_renderer ) {
        alt_rect_tex_enabled = false;
        if( get_option<bool>( "FRAMEBUFFER_ACCEL" ) ) {
            SDL_SetHint( SDL_HINT_FRAMEBUFFER_ACCELERATION, "1" );
        }
        renderer.reset( SDL_CreateRenderer( ::window.get(), -1,
                                            SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE ) );
        throwErrorIf( !renderer, "Failed to initialize software renderer" );
        throwErrorIf( !SetupRenderTarget(),
                      "Failed to initialize display buffer under software rendering, unable to continue." );
    }

    SDL_SetWindowMinimumSize( ::window.get(), fontwidth * FULL_SCREEN_WIDTH * scaling_factor,
                              fontheight * FULL_SCREEN_HEIGHT * scaling_factor );

    ClearScreen();

    // Errors here are ignored, worst case: the option does not work as expected,
    // but that won't crash
    if( get_option<std::string>( "HIDE_CURSOR" ) != "show" && SDL_ShowCursor( -1 ) ) {
        SDL_ShowCursor( SDL_DISABLE );
    } else {
        SDL_ShowCursor( SDL_ENABLE );
    }

    // Initialize joysticks.
    int numjoy = SDL_NumJoysticks();

    if( get_option<bool>( "ENABLE_JOYSTICK" ) && numjoy >= 1 ) {
        if( numjoy > 1 ) {
            dbg( D_WARNING ) <<
                             "You have more than one gamepads/joysticks plugged in, only the first will be used.";
        }
        joystick = SDL_JoystickOpen( 0 );
        printErrorIf( joystick == nullptr, "SDL_JoystickOpen failed" );
        if( joystick ) {
            printErrorIf( SDL_JoystickEventState( SDL_ENABLE ) < 0,
                          "SDL_JoystickEventState(SDL_ENABLE) failed" );
        }
    } else {
        joystick = nullptr;
    }

    // Set up audio mixer.
    init_sound();

    DebugLog( D_INFO, DC_ALL ) << "USE_COLOR_MODULATED_TEXTURES is set to " <<
                               get_option<bool>( "USE_COLOR_MODULATED_TEXTURES" );
    //initialize the alternate rectangle texture for replacing SDL_RenderFillRect
    if( get_option<bool>( "USE_COLOR_MODULATED_TEXTURES" ) ) {
        generate_alt_rect_texture();
    }

}

static void WinDestroy()
{
    shutdown_sound();
    tilecontext.reset();

    if( joystick ) {
        SDL_JoystickClose( joystick );
        alt_rect_tex.reset();

        joystick = nullptr;
    }
    format.reset();
    display_buffer.reset();
    renderer.reset();
    ::window.reset();
}

inline void FillRectDIB_SDLColor( const SDL_Rect &rect, const SDL_Color &color )
{
    if( alt_rect_tex_enabled ) {
        draw_alt_rect( renderer, rect, color.r, color.g, color.b );
    } else {
        SetRenderDrawColor( renderer, color.r, color.g, color.b, color.a );
        RenderFillRect( renderer, &rect );
    }
}

inline void FillRectDIB( const SDL_Rect &rect, const unsigned char color,
                         const unsigned char alpha = 255 )
{
    const SDL_Color sdl_color = { windowsPalette[color].r, windowsPalette[color].g, windowsPalette[color].b, alpha };
    FillRectDIB_SDLColor( rect, sdl_color );
}

//The following 3 methods use mem functions for fast drawing
inline void VertLineDIB( int x, int y, int y2, int thickness, unsigned char color )
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = thickness;
    rect.h = y2 - y;
    FillRectDIB( rect, color );
}
inline void HorzLineDIB( int x, int y, int x2, int thickness, unsigned char color )
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = x2 - x;
    rect.h = thickness;
    FillRectDIB( rect, color );
}
inline void FillRectDIB( int x, int y, int width, int height, unsigned char color )
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = width;
    rect.h = height;
    FillRectDIB( rect, color );
}

inline void fill_rect_xywh_color( const int x, const int y, const int width, const int height,
                                  const SDL_Color &color )
{
    const SDL_Rect rect = { x, y, width, height };
    FillRectDIB_SDLColor( rect, color );
}

SDL_Texture_Ptr CachedTTFFont::create_glyph( const std::string &ch, const int color )
{
    const auto function = fontblending ? TTF_RenderUTF8_Blended : TTF_RenderUTF8_Solid;
    SDL_Surface_Ptr sglyph( function( font.get(), ch.c_str(), windowsPalette[color] ) );
    if( !sglyph ) {
        dbg( D_ERROR ) << "Failed to create glyph for " << ch << ": " << TTF_GetError();
        return nullptr;
    }
    /* SDL interprets each pixel as a 32-bit number, so our masks must depend
       on the endianness (byte order) of the machine */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    static const Uint32 rmask = 0xff000000;
    static const Uint32 gmask = 0x00ff0000;
    static const Uint32 bmask = 0x0000ff00;
    static const Uint32 amask = 0x000000ff;
#else
    static const Uint32 rmask = 0x000000ff;
    static const Uint32 gmask = 0x0000ff00;
    static const Uint32 bmask = 0x00ff0000;
    static const Uint32 amask = 0xff000000;
#endif
    const int wf = utf8_wrapper( ch ).display_width();
    // Note: bits per pixel must be 8 to be synchronized with the surface
    // that TTF_RenderGlyph above returns. This is important for SDL_BlitScaled
    SDL_Surface_Ptr surface = CreateRGBSurface( 0, fontwidth * wf, fontheight, 32, rmask, gmask, bmask,
                              amask );
    SDL_Rect src_rect = { 0, 0, sglyph->w, sglyph->h };
    SDL_Rect dst_rect = { 0, 0, fontwidth * wf, fontheight };
    if( src_rect.w < dst_rect.w ) {
        dst_rect.x = ( dst_rect.w - src_rect.w ) / 2;
        dst_rect.w = src_rect.w;
    } else if( src_rect.w > dst_rect.w ) {
        src_rect.x = ( src_rect.w - dst_rect.w ) / 2;
        src_rect.w = dst_rect.w;
    }
    if( src_rect.h < dst_rect.h ) {
        dst_rect.y = ( dst_rect.h - src_rect.h ) / 2;
        dst_rect.h = src_rect.h;
    } else if( src_rect.h > dst_rect.h ) {
        src_rect.y = ( src_rect.h - dst_rect.h ) / 2;
        src_rect.h = dst_rect.h;
    }

    if( !printErrorIf( SDL_BlitSurface( sglyph.get(), &src_rect, surface.get(), &dst_rect ) != 0,
                       "SDL_BlitSurface failed" ) ) {
        sglyph = std::move( surface );
    }

    return CreateTextureFromSurface( renderer, sglyph );
}

bool CachedTTFFont::isGlyphProvided( const std::string &ch ) const
{
    return TTF_GlyphIsProvided( font.get(), UTF8_getch( ch ) );
}

void CachedTTFFont::OutputChar( const std::string &ch, const int x, const int y,
                                const unsigned char color, const float opacity )
{
    key_t    key {ch, static_cast<unsigned char>( color & 0xf )};

    auto it = glyph_cache_map.find( key );
    if( it == std::end( glyph_cache_map ) ) {
        cached_t new_entry {
            create_glyph( key.codepoints, key.color ),
            static_cast<int>( fontwidth * utf8_wrapper( key.codepoints ).display_width() )
        };
        it = glyph_cache_map.insert( std::make_pair( std::move( key ), std::move( new_entry ) ) ).first;
    }
    const cached_t &value = it->second;

    if( !value.texture ) {
        // Nothing we can do here )-:
        return;
    }
    SDL_Rect rect {x, y, value.width, fontheight};
    if( opacity != 1.0f ) {
        SDL_SetTextureAlphaMod( value.texture.get(), opacity * 255.0f );
    }
    RenderCopy( renderer, value.texture, nullptr, &rect );
    if( opacity != 1.0f ) {
        SDL_SetTextureAlphaMod( value.texture.get(), 255 );
    }
}

bool BitmapFont::isGlyphProvided( const std::string &ch ) const
{
    const uint32_t t = UTF8_getch( ch );
    switch( t ) {
        case LINE_XOXO_UNICODE:
        case LINE_OXOX_UNICODE:
        case LINE_XXOO_UNICODE:
        case LINE_OXXO_UNICODE:
        case LINE_OOXX_UNICODE:
        case LINE_XOOX_UNICODE:
        case LINE_XXXO_UNICODE:
        case LINE_XXOX_UNICODE:
        case LINE_XOXX_UNICODE:
        case LINE_OXXX_UNICODE:
        case LINE_XXXX_UNICODE:
            return true;
        default:
            return t < 256;
    }
}

void BitmapFont::OutputChar( const std::string &ch, const int x, const int y,
                             const unsigned char color, const float opacity )
{
    const int t = UTF8_getch( ch );
    BitmapFont::OutputChar( t, x, y, color, opacity );
}

void BitmapFont::OutputChar( const int t, const int x, const int y,
                             const unsigned char color, const float opacity )
{
    if( t <= 256 ) {
        SDL_Rect src;
        src.x = ( t % tilewidth ) * fontwidth;
        src.y = ( t / tilewidth ) * fontheight;
        src.w = fontwidth;
        src.h = fontheight;
        SDL_Rect rect;
        rect.x = x;
        rect.y = y;
        rect.w = fontwidth;
        rect.h = fontheight;
        if( opacity != 1.0f ) {
            SDL_SetTextureAlphaMod( ascii[color].get(), opacity * 255 );
        }
        RenderCopy( renderer, ascii[color], &src, &rect );
        if( opacity != 1.0f ) {
            SDL_SetTextureAlphaMod( ascii[color].get(), 255 );
        }
    } else {
        unsigned char uc = 0;
        switch( t ) {
            case LINE_XOXO_UNICODE:
                uc = LINE_XOXO_C;
                break;
            case LINE_OXOX_UNICODE:
                uc = LINE_OXOX_C;
                break;
            case LINE_XXOO_UNICODE:
                uc = LINE_XXOO_C;
                break;
            case LINE_OXXO_UNICODE:
                uc = LINE_OXXO_C;
                break;
            case LINE_OOXX_UNICODE:
                uc = LINE_OOXX_C;
                break;
            case LINE_XOOX_UNICODE:
                uc = LINE_XOOX_C;
                break;
            case LINE_XXXO_UNICODE:
                uc = LINE_XXXO_C;
                break;
            case LINE_XXOX_UNICODE:
                uc = LINE_XXOX_C;
                break;
            case LINE_XOXX_UNICODE:
                uc = LINE_XOXX_C;
                break;
            case LINE_OXXX_UNICODE:
                uc = LINE_OXXX_C;
                break;
            case LINE_XXXX_UNICODE:
                uc = LINE_XXXX_C;
                break;
            default:
                return;
        }
        draw_ascii_lines( uc, x, y, color );
    }
}

void refresh_display()
{
    needupdate = false;
    lastupdate = SDL_GetTicks();

    if( test_mode ) {
        return;
    }

    // Select default target (the window), copy rendered buffer
    // there, present it, select the buffer as target again.
    SetRenderTarget( renderer, nullptr );
    RenderCopy( renderer, display_buffer, nullptr, nullptr );
    SDL_RenderPresent( renderer.get() );
    SetRenderTarget( renderer, display_buffer );
}

// only update if the set interval has elapsed
static void try_sdl_update()
{
    uint32_t now = SDL_GetTicks();
    if( now - lastupdate >= interval ) {
        refresh_display();
    } else {
        needupdate = true;
    }
}

//for resetting the render target after updating texture caches in cata_tiles.cpp
void set_displaybuffer_rendertarget()
{
    SetRenderTarget( renderer, display_buffer );
}

// line_id is one of the LINE_*_C constants
// FG is a curses color
void CataFont::draw_ascii_lines( unsigned char line_id, int drawx, int drawy, int FG ) const
{
    switch( line_id ) {
        // box bottom/top side (horizontal line)
        case LINE_OXOX_C:
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            break;
        // box left/right side (vertical line)
        case LINE_XOXO_C:
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + fontheight, 2, FG );
            break;
        // box top left
        case LINE_OXXO_C:
            HorzLineDIB( drawx + ( fontwidth / 2 ), drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy + ( fontheight / 2 ), drawy + fontheight, 2, FG );
            break;
        // box top right
        case LINE_OOXX_C:
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + ( fontwidth / 2 ), 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy + ( fontheight / 2 ), drawy + fontheight, 2, FG );
            break;
        // box bottom right
        case LINE_XOOX_C:
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + ( fontwidth / 2 ), 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + ( fontheight / 2 ) + 1, 2, FG );
            break;
        // box bottom left
        case LINE_XXOO_C:
            HorzLineDIB( drawx + ( fontwidth / 2 ), drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + ( fontheight / 2 ) + 1, 2, FG );
            break;
        // box bottom north T (left, right, up)
        case LINE_XXOX_C:
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + ( fontheight / 2 ), 2, FG );
            break;
        // box bottom east T (up, right, down)
        case LINE_XXXO_C:
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + fontheight, 2, FG );
            HorzLineDIB( drawx + ( fontwidth / 2 ), drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            break;
        // box bottom south T (left, right, down)
        case LINE_OXXX_C:
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy + ( fontheight / 2 ), drawy + fontheight, 2, FG );
            break;
        // box X (left down up right)
        case LINE_XXXX_C:
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + fontwidth, 1, FG );
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + fontheight, 2, FG );
            break;
        // box bottom east T (left, down, up)
        case LINE_XOXX_C:
            VertLineDIB( drawx + ( fontwidth / 2 ), drawy, drawy + fontheight, 2, FG );
            HorzLineDIB( drawx, drawy + ( fontheight / 2 ), drawx + ( fontwidth / 2 ), 1, FG );
            break;
        default:
            break;
    }
}

static void invalidate_framebuffer( std::vector<curseline> &framebuffer, int x, int y, int width,
                                    int height )
{
    for( int j = 0, fby = y; j < height; j++, fby++ ) {
        std::fill_n( framebuffer[fby].chars.begin() + x, width, cursecell( "" ) );
    }
}

static void invalidate_framebuffer( std::vector<curseline> &framebuffer )
{
    for( curseline &i : framebuffer ) {
        std::fill_n( i.chars.begin(), i.chars.size(), cursecell( "" ) );
    }
}

void reinitialize_framebuffer()
{
    //Re-initialize the framebuffer with new values.
    const int new_height = std::max( TERMY, std::max( OVERMAP_WINDOW_HEIGHT, TERRAIN_WINDOW_HEIGHT ) );
    const int new_width = std::max( TERMX, std::max( OVERMAP_WINDOW_WIDTH, TERRAIN_WINDOW_WIDTH ) );
    oversized_framebuffer.resize( new_height );
    for( int i = 0; i < new_height; i++ ) {
        oversized_framebuffer[i].chars.assign( new_width, cursecell( "" ) );
    }
    terminal_framebuffer.resize( new_height );
    for( int i = 0; i < new_height; i++ ) {
        terminal_framebuffer[i].chars.assign( new_width, cursecell( "" ) );
    }
}

static void invalidate_framebuffer_proportion( cata_cursesport::WINDOW *win )
{
    const int oversized_width = std::max( TERMX, std::max( OVERMAP_WINDOW_WIDTH,
                                          TERRAIN_WINDOW_WIDTH ) );
    const int oversized_height = std::max( TERMY, std::max( OVERMAP_WINDOW_HEIGHT,
                                           TERRAIN_WINDOW_HEIGHT ) );

    // check if the framebuffers/windows have been prepared yet
    if( oversized_height == 0 || oversized_width == 0 ) {
        return;
    }
    if( !g || win == nullptr ) {
        return;
    }
    if( win == g->w_overmap || win == g->w_terrain || win == w_hit_animation ) {
        return;
    }

    // track the dimensions for conversion
    const int termpixel_x = win->pos.x * font->fontwidth;
    const int termpixel_y = win->pos.y * font->fontheight;
    const int termpixel_x2 = termpixel_x + win->width * font->fontwidth - 1;
    const int termpixel_y2 = termpixel_y + win->height * font->fontheight - 1;

    if( map_font != nullptr && map_font->fontwidth != 0 && map_font->fontheight != 0 ) {
        const int mapfont_x = termpixel_x / map_font->fontwidth;
        const int mapfont_y = termpixel_y / map_font->fontheight;
        const int mapfont_x2 = std::min( termpixel_x2 / map_font->fontwidth, oversized_width - 1 );
        const int mapfont_y2 = std::min( termpixel_y2 / map_font->fontheight, oversized_height - 1 );
        const int mapfont_width = mapfont_x2 - mapfont_x + 1;
        const int mapfont_height = mapfont_y2 - mapfont_y + 1;
        invalidate_framebuffer( oversized_framebuffer, mapfont_x, mapfont_y, mapfont_width,
                                mapfont_height );
    }

    if( overmap_font != nullptr && overmap_font->fontwidth != 0 && overmap_font->fontheight != 0 ) {
        const int overmapfont_x = termpixel_x / overmap_font->fontwidth;
        const int overmapfont_y = termpixel_y / overmap_font->fontheight;
        const int overmapfont_x2 = std::min( termpixel_x2 / overmap_font->fontwidth, oversized_width - 1 );
        const int overmapfont_y2 = std::min( termpixel_y2 / overmap_font->fontheight,
                                             oversized_height - 1 );
        const int overmapfont_width = overmapfont_x2 - overmapfont_x + 1;
        const int overmapfont_height = overmapfont_y2 - overmapfont_y + 1;
        invalidate_framebuffer( oversized_framebuffer, overmapfont_x, overmapfont_y, overmapfont_width,
                                overmapfont_height );
    }
}

// clear the framebuffer when werase is called on certain windows that don't use the main terminal font
void cata_cursesport::handle_additional_window_clear( WINDOW *win )
{
    if( !g ) {
        return;
    }
    if( win == g->w_terrain || win == g->w_overmap ) {
        invalidate_framebuffer( oversized_framebuffer );
    }
}

void clear_window_area( const catacurses::window &win_ )
{
    cata_cursesport::WINDOW *const win = win_.get<cata_cursesport::WINDOW>();
    FillRectDIB( win->pos.x * fontwidth, win->pos.y * fontheight,
                 win->width * fontwidth, win->height * fontheight, catacurses::black );
}

void cata_cursesport::curses_drawwindow( const catacurses::window &w )
{
    if( scaling_factor > 1 ) {
        SDL_RenderSetLogicalSize( renderer.get(), WindowWidth / scaling_factor,
                                  WindowHeight / scaling_factor );
    }
    WINDOW *const win = w.get<WINDOW>();
    bool update = false;
    if( g && w == g->w_terrain && use_tiles ) {
        // color blocks overlay; drawn on top of tiles and on top of overlay strings (if any).
        color_block_overlay_container color_blocks;

        // Strings with colors do be drawn with map_font on top of tiles.
        std::multimap<point, formatted_text> overlay_strings;

        // game::w_terrain can be drawn by the tilecontext.
        // skip the normal drawing code for it.
        tilecontext->draw(
            point( win->pos.x * fontwidth, win->pos.y * fontheight ),
            g->ter_view_p,
            TERRAIN_WINDOW_TERM_WIDTH * font->fontwidth,
            TERRAIN_WINDOW_TERM_HEIGHT * font->fontheight,
            overlay_strings,
            color_blocks );

        // color blocks overlay
        if( !color_blocks.second.empty() ) {
            SDL_BlendMode blend_mode;
            GetRenderDrawBlendMode( renderer, blend_mode ); // save the current blend mode
            SetRenderDrawBlendMode( renderer, color_blocks.first ); // set the new blend mode
            for( const auto &e : color_blocks.second ) {
                fill_rect_xywh_color( e.first.x, e.first.y, tilecontext->get_tile_width(),
                                      tilecontext->get_tile_height(), e.second );
            }
            SetRenderDrawBlendMode( renderer, blend_mode ); // set the old blend mode
        }

        // overlay strings
        point prev_coord;
        int x_offset = 0;
        int alignment_offset = 0;
        for( const auto &iter : overlay_strings ) {
            const point coord = iter.first;
            const formatted_text ft = iter.second;
            const utf8_wrapper text( ft.text );

            // Strings at equal coords are displayed sequentially.
            if( coord != prev_coord ) {
                x_offset = 0;
            }

            // Calculate length of all strings in sequence to align them.
            if( x_offset == 0 ) {
                int full_text_length = 0;
                const auto range = overlay_strings.equal_range( coord );
                for( auto ri = range.first; ri != range.second; ++ri ) {
                    utf8_wrapper rt( ri->second.text );
                    full_text_length += rt.display_width();
                }

                alignment_offset = 0;
                if( ft.alignment == TEXT_ALIGNMENT_CENTER ) {
                    alignment_offset = full_text_length / 2;
                } else if( ft.alignment == TEXT_ALIGNMENT_RIGHT ) {
                    alignment_offset = full_text_length - 1;
                }
            }

            int width = 0;
            for( size_t i = 0; i < text.size(); ++i ) {
                const int x0 = win->pos.x * fontwidth;
                const int y0 = win->pos.y * fontheight;
                const int x = x0 + ( x_offset - alignment_offset + width ) * map_font->fontwidth + coord.x;
                const int y = y0 + coord.y;

                // Clip to window bounds.
                if( x < x0 || x > x0 + ( TERRAIN_WINDOW_TERM_WIDTH - 1 ) * font->fontwidth
                    || y < y0 || y > y0 + ( TERRAIN_WINDOW_TERM_HEIGHT - 1 ) * font->fontheight ) {
                    continue;
                }

                // TODO: draw with outline / BG color for better readability
                const uint32_t ch = text.at( i );
                map_font->OutputChar( utf32_to_utf8( ch ), x, y, ft.color );
                width += mk_wcwidth( ch );
            }

            prev_coord = coord;
            x_offset = width;
        }

        invalidate_framebuffer( terminal_framebuffer, win->pos.x, win->pos.y,
                                TERRAIN_WINDOW_TERM_WIDTH, TERRAIN_WINDOW_TERM_HEIGHT );

        update = true;
    } else if( g && w == g->w_terrain && map_font ) {
        // When the terrain updates, predraw a black space around its edge
        // to keep various former interface elements from showing through the gaps
        // TODO: Maybe track down screen changes and use g->w_blackspace to draw this instead

        //calculate width differences between map_font and font
        int partial_width = std::max( TERRAIN_WINDOW_TERM_WIDTH * fontwidth - TERRAIN_WINDOW_WIDTH *
                                      map_font->fontwidth, 0 );
        int partial_height = std::max( TERRAIN_WINDOW_TERM_HEIGHT * fontheight - TERRAIN_WINDOW_HEIGHT *
                                       map_font->fontheight, 0 );
        //Gap between terrain and lower window edge
        if( partial_height > 0 ) {
            FillRectDIB( win->pos.x * map_font->fontwidth,
                         ( win->pos.y + TERRAIN_WINDOW_HEIGHT ) * map_font->fontheight,
                         TERRAIN_WINDOW_WIDTH * map_font->fontwidth + partial_width, partial_height, catacurses::black );
        }
        //Gap between terrain and sidebar
        if( partial_width > 0 ) {
            FillRectDIB( ( win->pos.x + TERRAIN_WINDOW_WIDTH ) * map_font->fontwidth,
                         win->pos.y * map_font->fontheight,
                         partial_width,
                         TERRAIN_WINDOW_HEIGHT * map_font->fontheight + partial_height,
                         catacurses::black );
        }
        // Special font for the terrain window
        update = map_font->draw_window( w );
    } else if( g && w == g->w_overmap && overmap_font ) {
        // Special font for the terrain window
        update = overmap_font->draw_window( w );
    } else if( w == w_hit_animation && map_font ) {
        // The animation window overlays the terrain window,
        // it uses the same font, but it's only 1 square in size.
        // The offset must not use the global font, but the map font
        int offsetx = win->pos.x * map_font->fontwidth;
        int offsety = win->pos.y * map_font->fontheight;
        update = map_font->draw_window( w, offsetx, offsety );
    } else if( g && w == g->w_blackspace ) {
        // fill-in black space window skips draw code
        // so as not to confuse framebuffer any more than necessary
        int offsetx = win->pos.x * font->fontwidth;
        int offsety = win->pos.y * font->fontheight;
        int wwidth = win->width * font->fontwidth;
        int wheight = win->height * font->fontheight;
        FillRectDIB( offsetx, offsety, wwidth, wheight, catacurses::black );
        update = true;
    } else if( g && w == g->w_pixel_minimap && g->pixel_minimap_option ) {
        // ensure the space the minimap covers is "dirtied".
        // this is necessary when it's the only part of the sidebar being drawn
        // TODO: Figure out how to properly make the minimap code do whatever it is this does
        font->draw_window( w );

        // Make sure the entire minimap window is black before drawing.
        clear_window_area( w );
        tilecontext->draw_minimap(
            point( win->pos.x * fontwidth, win->pos.y * fontheight ),
            tripoint( g->u.pos().xy(), g->ter_view_p.z ),
            win->width * font->fontwidth, win->height * font->fontheight );
        update = true;

    } else {
        // Either not using tiles (tilecontext) or not the w_terrain window.
        update = font->draw_window( w );
    }
    if( update ) {
        needupdate = true;
    }
}

bool CataFont::draw_window( const catacurses::window &w )
{
    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();
    // Use global font sizes here to make this independent of the
    // font used for this window.
    return draw_window( w, win->pos.x * ::fontwidth, win->pos.y * ::fontheight );
}

bool CataFont::draw_window( const catacurses::window &w, const int offsetx, const int offsety )
{
    if( scaling_factor > 1 ) {
        SDL_RenderSetLogicalSize( renderer.get(), WindowWidth / scaling_factor,
                                  WindowHeight / scaling_factor );
    }

    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();
    //Keeping track of the last drawn window
    const cata_cursesport::WINDOW *winBuffer = static_cast<cata_cursesport::WINDOW *>
            ( ::winBuffer.lock().get() );
    if( !fontScaleBuffer ) {
        fontScaleBuffer = tilecontext->get_tile_width();
    }
    const int fontScale = tilecontext->get_tile_width();
    //This creates a problem when map_font is different from the regular font
    //Specifically when showing the overmap
    //And in some instances of screen change, i.e. inventory.
    bool oldWinCompatible = false;

    // clear the oversized buffer proportionally
    invalidate_framebuffer_proportion( win );

    // use the oversize buffer when dealing with windows that can have a different font than the main text font
    bool use_oversized_framebuffer = g && ( w == g->w_terrain || w == g->w_overmap ||
                                            w == w_hit_animation );

    std::vector<curseline> &framebuffer = use_oversized_framebuffer ? oversized_framebuffer :
                                          terminal_framebuffer;

    /*
    Let's try to keep track of different windows.
    A number of windows are coexisting on the screen, so don't have to interfere.

    g->w_terrain, g->w_minimap, g->w_HP, g->w_status, g->w_status2, g->w_messages,
     g->w_location, and g->w_minimap, can be buffered if either of them was
     the previous window.

    g->w_overmap and g->w_omlegend are likewise.

    Everything else works on strict equality because there aren't yet IDs for some of them.
    */
    if( g && ( w == g->w_terrain || w == g->w_minimap ) ) {
        if( winBuffer == g->w_terrain || winBuffer == g->w_minimap ) {
            oldWinCompatible = true;
        }
    } else if( g && ( w == g->w_overmap || w == g->w_omlegend ) ) {
        if( winBuffer == g->w_overmap || winBuffer == g->w_omlegend ) {
            oldWinCompatible = true;
        }
    } else {
        if( win == winBuffer ) {
            oldWinCompatible = true;
        }
    }

    // TODO: Get this from UTF system to make sure it is exactly the kind of space we need
    static const std::string space_string = " ";

    bool update = false;
    for( int j = 0; j < win->height; j++ ) {
        if( !win->line[j].touched ) {
            continue;
        }

        const int fby = win->pos.y + j;
        if( fby >= static_cast<int>( framebuffer.size() ) ) {
            // prevent indexing outside the frame buffer. This might happen for some parts of the window. FIX #28953.
            break;
        }

        update = true;
        win->line[j].touched = false;
        for( int i = 0; i < win->width; i++ ) {
            const int fbx = win->pos.x + i;
            if( fbx >= static_cast<int>( framebuffer[fby].chars.size() ) ) {
                // prevent indexing outside the frame buffer. This might happen for some parts of the window.
                break;
            }

            const cursecell &cell = win->line[j].chars[i];

            const int drawx = offsetx + i * fontwidth;
            const int drawy = offsety + j * fontheight;
            if( drawx + fontwidth > WindowWidth || drawy + fontheight > WindowHeight ) {
                // Outside of the display area, would not render anyway
                continue;
            }

            // Avoid redrawing an unchanged tile by checking the framebuffer cache
            // TODO: handle caching when drawing normal windows over graphical tiles
            cursecell &oldcell = framebuffer[fby].chars[fbx];

            if( oldWinCompatible && cell == oldcell && fontScale == fontScaleBuffer ) {
                continue;
            }
            oldcell = cell;

            if( cell.ch.empty() ) {
                continue; // second cell of a multi-cell character
            }

            // Spaces are used a lot, so this does help noticeably
            if( cell.ch == space_string ) {
                FillRectDIB( drawx, drawy, fontwidth, fontheight, cell.BG );
                continue;
            }
            const int codepoint = UTF8_getch( cell.ch );
            const catacurses::base_color FG = cell.FG;
            const catacurses::base_color BG = cell.BG;
            int cw = ( codepoint == UNKNOWN_UNICODE ) ? 1 : utf8_width( cell.ch );
            if( cw < 1 ) {
                // utf8_width() may return a negative width
                continue;
            }
            bool use_draw_ascii_lines_routine = get_option<bool>( "USE_DRAW_ASCII_LINES_ROUTINE" );
            unsigned char uc = static_cast<unsigned char>( cell.ch[0] );
            switch( codepoint ) {
                case LINE_XOXO_UNICODE:
                    uc = LINE_XOXO_C;
                    break;
                case LINE_OXOX_UNICODE:
                    uc = LINE_OXOX_C;
                    break;
                case LINE_XXOO_UNICODE:
                    uc = LINE_XXOO_C;
                    break;
                case LINE_OXXO_UNICODE:
                    uc = LINE_OXXO_C;
                    break;
                case LINE_OOXX_UNICODE:
                    uc = LINE_OOXX_C;
                    break;
                case LINE_XOOX_UNICODE:
                    uc = LINE_XOOX_C;
                    break;
                case LINE_XXXO_UNICODE:
                    uc = LINE_XXXO_C;
                    break;
                case LINE_XXOX_UNICODE:
                    uc = LINE_XXOX_C;
                    break;
                case LINE_XOXX_UNICODE:
                    uc = LINE_XOXX_C;
                    break;
                case LINE_OXXX_UNICODE:
                    uc = LINE_OXXX_C;
                    break;
                case LINE_XXXX_UNICODE:
                    uc = LINE_XXXX_C;
                    break;
                case UNKNOWN_UNICODE:
                    use_draw_ascii_lines_routine = true;
                    break;
                default:
                    use_draw_ascii_lines_routine = false;
                    break;
            }
            FillRectDIB( drawx, drawy, fontwidth * cw, fontheight, BG );
            if( use_draw_ascii_lines_routine ) {
                draw_ascii_lines( uc, drawx, drawy, FG );
            } else {
                OutputChar( cell.ch, drawx, drawy, FG );
            }
        }
    }
    win->draw = false; //We drew the window, mark it as so
    //Keeping track of last drawn window and tilemode zoom level
    ::winBuffer = w.weak_ptr();
    fontScaleBuffer = tilecontext->get_tile_width();

    return update;
}

static int alt_buffer = 0;
static bool alt_down = false;

static void begin_alt_code()
{
    alt_buffer = 0;
    alt_down = true;
}

static bool add_alt_code( char c )
{
    if( alt_down && c >= '0' && c <= '9' ) {
        alt_buffer = alt_buffer * 10 + ( c - '0' );
        return true;
    }
    return false;
}

static int end_alt_code()
{
    alt_down = false;
    return alt_buffer;
}

static int HandleDPad()
{
    // Check if we have a gamepad d-pad event.
    if( SDL_JoystickGetHat( joystick, 0 ) != SDL_HAT_CENTERED ) {
        // When someone tries to press a diagonal, they likely will
        // press a single direction first. Wait a few milliseconds to
        // give them time to press both of the buttons for the diagonal.
        int button = SDL_JoystickGetHat( joystick, 0 );
        int lc = ERR;
        if( button == SDL_HAT_LEFT ) {
            lc = JOY_LEFT;
        } else if( button == SDL_HAT_DOWN ) {
            lc = JOY_DOWN;
        } else if( button == SDL_HAT_RIGHT ) {
            lc = JOY_RIGHT;
        } else if( button == SDL_HAT_UP ) {
            lc = JOY_UP;
        } else if( button == SDL_HAT_LEFTUP ) {
            lc = JOY_LEFTUP;
        } else if( button == SDL_HAT_LEFTDOWN ) {
            lc = JOY_LEFTDOWN;
        } else if( button == SDL_HAT_RIGHTUP ) {
            lc = JOY_RIGHTUP;
        } else if( button == SDL_HAT_RIGHTDOWN ) {
            lc = JOY_RIGHTDOWN;
        }

        if( delaydpad == std::numeric_limits<Uint32>::max() ) {
            delaydpad = SDL_GetTicks() + dpad_delay;
            queued_dpad = lc;
        }

        // Okay it seems we're ready to process.
        if( SDL_GetTicks() > delaydpad ) {

            if( lc != ERR ) {
                if( dpad_continuous && ( lc & lastdpad ) == 0 ) {
                    // Continuous movement should only work in the same or similar directions.
                    dpad_continuous = false;
                    lastdpad = lc;
                    return 0;
                }

                last_input = input_event( lc, CATA_INPUT_GAMEPAD );
                lastdpad = lc;
                queued_dpad = ERR;

                if( !dpad_continuous ) {
                    delaydpad = SDL_GetTicks() + 200;
                    dpad_continuous = true;
                } else {
                    delaydpad = SDL_GetTicks() + 60;
                }
                return 1;
            }
        }
    } else {
        dpad_continuous = false;
        delaydpad = std::numeric_limits<Uint32>::max();

        // If we didn't hold it down for a while, just
        // fire the last registered press.
        if( queued_dpad != ERR ) {
            last_input = input_event( queued_dpad, CATA_INPUT_GAMEPAD );
            queued_dpad = ERR;
            return 1;
        }
    }

    return 0;
}

static SDL_Keycode sdl_keycode_opposite_arrow( SDL_Keycode key )
{
    switch( key ) {
        case SDLK_UP:
            return SDLK_DOWN;
        case SDLK_DOWN:
            return SDLK_UP;
        case SDLK_LEFT:
            return SDLK_RIGHT;
        case SDLK_RIGHT:
            return SDLK_LEFT;
    }
    return 0;
}

static bool sdl_keycode_is_arrow( SDL_Keycode key )
{
    return static_cast<bool>( sdl_keycode_opposite_arrow( key ) );
}

static int arrow_combo_to_numpad( SDL_Keycode mod, SDL_Keycode key )
{
    if( ( mod == SDLK_UP    && key == SDLK_RIGHT ) ||
        ( mod == SDLK_RIGHT && key == SDLK_UP ) ) {
        return KEY_NUM( 9 );
    }
    if( ( mod == SDLK_UP    && key == SDLK_UP ) ) {
        return KEY_NUM( 8 );
    }
    if( ( mod == SDLK_UP    && key == SDLK_LEFT ) ||
        ( mod == SDLK_LEFT  && key == SDLK_UP ) ) {
        return KEY_NUM( 7 );
    }
    if( ( mod == SDLK_RIGHT && key == SDLK_RIGHT ) ) {
        return KEY_NUM( 6 );
    }
    if( mod == sdl_keycode_opposite_arrow( key ) ) {
        return KEY_NUM( 5 );
    }
    if( ( mod == SDLK_LEFT  && key == SDLK_LEFT ) ) {
        return KEY_NUM( 4 );
    }
    if( ( mod == SDLK_DOWN  && key == SDLK_RIGHT ) ||
        ( mod == SDLK_RIGHT && key == SDLK_DOWN ) ) {
        return KEY_NUM( 3 );
    }
    if( ( mod == SDLK_DOWN  && key == SDLK_DOWN ) ) {
        return KEY_NUM( 2 );
    }
    if( ( mod == SDLK_DOWN  && key == SDLK_LEFT ) ||
        ( mod == SDLK_LEFT  && key == SDLK_DOWN ) ) {
        return KEY_NUM( 1 );
    }
    return 0;
}

static int arrow_combo_modifier = 0;

static int handle_arrow_combo( SDL_Keycode key )
{
    if( !arrow_combo_modifier ) {
        arrow_combo_modifier = key;
        return 0;
    }
    return arrow_combo_to_numpad( arrow_combo_modifier, key );
}

static void end_arrow_combo()
{
    arrow_combo_modifier = 0;
}

/**
 * Translate SDL key codes to key identifiers used by ncurses, this
 * allows the input_manager to only consider those.
 * @return 0 if the input can not be translated (unknown key?),
 * -1 when a ALT+number sequence has been started,
 * or something that a call to ncurses getch would return.
 */
static int sdl_keysym_to_curses( const SDL_Keysym &keysym )
{

    const std::string diag_mode = get_option<std::string>( "DIAG_MOVE_WITH_MODIFIERS_MODE" );

    if( diag_mode == "mode1" ) {
        if( keysym.mod & KMOD_CTRL && sdl_keycode_is_arrow( keysym.sym ) ) {
            return handle_arrow_combo( keysym.sym );
        } else {
            end_arrow_combo();
        }
    }

    if( diag_mode == "mode2" ) {
        //Shift + Cursor Arrow (diagonal clockwise)
        if( keysym.mod & KMOD_SHIFT ) {
            switch( keysym.sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTUP" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTDOWN" );
                case SDLK_UP:
                    return inp_mngr.get_first_char_for_action( "RIGHTUP" );
                case SDLK_DOWN:
                    return inp_mngr.get_first_char_for_action( "LEFTDOWN" );
            }
        }
        //Ctrl + Cursor Arrow (diagonal counter-clockwise)
        if( keysym.mod & KMOD_CTRL ) {
            switch( keysym.sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTDOWN" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTUP" );
                case SDLK_UP:
                    return inp_mngr.get_first_char_for_action( "LEFTUP" );
                case SDLK_DOWN:
                    return inp_mngr.get_first_char_for_action( "RIGHTDOWN" );
            }
        }
    }

    if( diag_mode == "mode3" ) {
        //Shift + Cursor Left/RightArrow
        if( keysym.mod & KMOD_SHIFT ) {
            switch( keysym.sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTUP" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTUP" );
            }
        }
        //Ctrl + Cursor Left/Right Arrow
        if( keysym.mod & KMOD_CTRL ) {
            switch( keysym.sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTDOWN" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTDOWN" );
            }
        }
    }

    if( keysym.mod & KMOD_CTRL && keysym.sym >= 'a' && keysym.sym <= 'z' ) {
        // ASCII ctrl codes, ^A through ^Z.
        return keysym.sym - 'a' + '\1';
    }
    switch( keysym.sym ) {
        // This is special: allow entering a Unicode character with ALT+number
        case SDLK_RALT:
        case SDLK_LALT:
            begin_alt_code();
            return -1;
        // The following are simple translations:
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
        case SDLK_RETURN2:
            return '\n';
        case SDLK_BACKSPACE:
        case SDLK_KP_BACKSPACE:
            return KEY_BACKSPACE;
        case SDLK_DELETE:
            return KEY_DC;
        case SDLK_ESCAPE:
            return KEY_ESCAPE;
        case SDLK_TAB:
            if( keysym.mod & KMOD_SHIFT ) {
                return KEY_BTAB;
            }
            return '\t';
        case SDLK_LEFT:
            return KEY_LEFT;
        case SDLK_RIGHT:
            return KEY_RIGHT;
        case SDLK_UP:
            return KEY_UP;
        case SDLK_DOWN:
            return KEY_DOWN;
        case SDLK_PAGEUP:
            return KEY_PPAGE;
        case SDLK_PAGEDOWN:
            return KEY_NPAGE;
        case SDLK_HOME:
            return KEY_HOME;
        case SDLK_END:
            return KEY_END;
        case SDLK_F1:
            return KEY_F( 1 );
        case SDLK_F2:
            return KEY_F( 2 );
        case SDLK_F3:
            return KEY_F( 3 );
        case SDLK_F4:
            return KEY_F( 4 );
        case SDLK_F5:
            return KEY_F( 5 );
        case SDLK_F6:
            return KEY_F( 6 );
        case SDLK_F7:
            return KEY_F( 7 );
        case SDLK_F8:
            return KEY_F( 8 );
        case SDLK_F9:
            return KEY_F( 9 );
        case SDLK_F10:
            return KEY_F( 10 );
        case SDLK_F11:
            return KEY_F( 11 );
        case SDLK_F12:
            return KEY_F( 12 );
        case SDLK_F13:
            return KEY_F( 13 );
        case SDLK_F14:
            return KEY_F( 14 );
        case SDLK_F15:
            return KEY_F( 15 );
        // Every other key is ignored as there is no curses constant for it.
        // TODO: add more if you find more.
        default:
            return 0;
    }
}

bool handle_resize( int w, int h )
{
    if( ( w != WindowWidth ) || ( h != WindowHeight ) ) {
        WindowWidth = w;
        WindowHeight = h;
        TERMINAL_WIDTH = WindowWidth / fontwidth / scaling_factor;
        TERMINAL_HEIGHT = WindowHeight / fontheight / scaling_factor;
        SetupRenderTarget();
        game_ui::init_ui();

        return true;
    }
    return false;
}

void toggle_fullscreen_window()
{
    static int restore_win_w = get_option<int>( "TERMINAL_X" ) * fontwidth * scaling_factor;
    static int restore_win_h = get_option<int>( "TERMINAL_Y" ) * fontheight * scaling_factor;

    if( fullscreen ) {
        if( printErrorIf( SDL_SetWindowFullscreen( window.get(), 0 ) != 0,
                          "SDL_SetWindowFullscreen failed" ) ) {
            return;
        }
        SDL_RestoreWindow( window.get() );
        SDL_SetWindowSize( window.get(), restore_win_w, restore_win_h );
        SDL_SetWindowMinimumSize( window.get(), fontwidth * FULL_SCREEN_WIDTH * scaling_factor,
                                  fontheight * FULL_SCREEN_HEIGHT * scaling_factor );
    } else {
        restore_win_w = WindowWidth;
        restore_win_h = WindowHeight;
        if( printErrorIf( SDL_SetWindowFullscreen( window.get(), SDL_WINDOW_FULLSCREEN_DESKTOP ) != 0,
                          "SDL_SetWindowFullscreen failed" ) ) {
            return;
        }
    }
    int nw = 0;
    int nh = 0;
    SDL_GetWindowSize( window.get(), &nw, &nh );
    handle_resize( nw, nh );
    fullscreen = !fullscreen;
}

//Check for any window messages (keypress, paint, mousemove, etc)
static void CheckMessages()
{
    SDL_Event ev;
    bool quit = false;
    bool text_refresh = false;
    bool is_repeat = false;
    if( HandleDPad() ) {
        return;
    }

    last_input = input_event();
    while( SDL_PollEvent( &ev ) ) {
        switch( ev.type ) {
            case SDL_WINDOWEVENT:
                switch( ev.window.event ) {
                    case SDL_WINDOWEVENT_SHOWN:
                    case SDL_WINDOWEVENT_EXPOSED:
                    case SDL_WINDOWEVENT_MINIMIZED:
                        break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        // Main menu redraw
                        reinitialize_framebuffer();
                        // TODO: redraw all game menus if they are open
                        needupdate = true;
                        break;
                    case SDL_WINDOWEVENT_RESTORED:
                        needupdate = true;
                        break;
                    case SDL_WINDOWEVENT_RESIZED:
                        needupdate = handle_resize( ev.window.data1, ev.window.data2 );
                        break;
                    default:
                        break;
                }
                break;
            case SDL_KEYDOWN: {
                is_repeat = ev.key.repeat;
                //hide mouse cursor on keyboard input
                if( get_option<std::string>( "HIDE_CURSOR" ) != "show" && SDL_ShowCursor( -1 ) ) {
                    SDL_ShowCursor( SDL_DISABLE );
                }
                const int lc = sdl_keysym_to_curses( ev.key.keysym );
                if( lc <= 0 ) {
                    // a key we don't know in curses and won't handle.
                    break;
                } else if( add_alt_code( lc ) ) {
                    // key was handled
                } else {
                    last_input = input_event( lc, CATA_INPUT_KEYBOARD );
                }
            }
            break;
            case SDL_KEYUP: {

                is_repeat = ev.key.repeat;
                if( ev.key.keysym.sym == SDLK_LALT || ev.key.keysym.sym == SDLK_RALT ) {
                    int code = end_alt_code();
                    if( code ) {
                        last_input = input_event( code, CATA_INPUT_KEYBOARD );
                        last_input.text = utf32_to_utf8( code );
                    }
                }
            }
            break;
            case SDL_TEXTINPUT:
                if( !add_alt_code( *ev.text.text ) ) {
                    if( strlen( ev.text.text ) > 0 ) {
                        const unsigned lc = UTF8_getch( ev.text.text );
                        last_input = input_event( lc, CATA_INPUT_KEYBOARD );

                    } else {
                        // no key pressed in this event
                        last_input = input_event();
                        last_input.type = CATA_INPUT_KEYBOARD;
                    }
                    last_input.text = ev.text.text;
                    text_refresh = true;
                }
                break;
            case SDL_TEXTEDITING: {
                if( strlen( ev.edit.text ) > 0 ) {
                    const unsigned lc = UTF8_getch( ev.edit.text );
                    last_input = input_event( lc, CATA_INPUT_KEYBOARD );
                } else {
                    // no key pressed in this event
                    last_input = input_event();
                    last_input.type = CATA_INPUT_KEYBOARD;
                }
                last_input.edit = ev.edit.text;
                last_input.edit_refresh = true;
                text_refresh = true;
            }
            break;
            case SDL_JOYBUTTONDOWN:
                last_input = input_event( ev.jbutton.button, CATA_INPUT_KEYBOARD );
                break;
            case SDL_JOYAXISMOTION:
                // on gamepads, the axes are the analog sticks
                // TODO: somehow get the "digipad" values from the axes
                break;
            case SDL_MOUSEMOTION:
                if( get_option<std::string>( "HIDE_CURSOR" ) == "show" ||
                    get_option<std::string>( "HIDE_CURSOR" ) == "hidekb" ) {
                    if( !SDL_ShowCursor( -1 ) ) {
                        SDL_ShowCursor( SDL_ENABLE );
                    }

                    // Only monitor motion when cursor is visible
                    last_input = input_event( MOUSE_MOVE, CATA_INPUT_MOUSE );
                }
                break;

            case SDL_MOUSEBUTTONUP:
                switch( ev.button.button ) {
                    case SDL_BUTTON_LEFT:
                        last_input = input_event( MOUSE_BUTTON_LEFT, CATA_INPUT_MOUSE );
                        break;
                    case SDL_BUTTON_RIGHT:
                        last_input = input_event( MOUSE_BUTTON_RIGHT, CATA_INPUT_MOUSE );
                        break;
                }
                break;

            case SDL_MOUSEWHEEL:
                if( ev.wheel.y > 0 ) {
                    last_input = input_event( SCROLLWHEEL_UP, CATA_INPUT_MOUSE );
                } else if( ev.wheel.y < 0 ) {
                    last_input = input_event( SCROLLWHEEL_DOWN, CATA_INPUT_MOUSE );
                }
                break;

            case SDL_QUIT:
                quit = true;
                break;
        }
        if( text_refresh && !is_repeat ) {
            break;
        }
    }
    if( needupdate ) {
        try_sdl_update();
    }
    if( quit ) {
        catacurses::endwin();
        exit( 0 );
    }
}

// Check if text ends with suffix
static bool ends_with( const std::string &text, const std::string &suffix )
{
    return text.length() >= suffix.length() &&
           strcasecmp( text.c_str() + text.length() - suffix.length(), suffix.c_str() ) == 0;
}

//***********************************
//Pseudo-Curses Functions           *
//***********************************

static void font_folder_list( std::ostream &fout, const std::string &path,
                              std::set<std::string> &bitmap_fonts )
{
    for( const std::string &f : get_files_from_path( "", path, true, false ) ) {
        TTF_Font_Ptr fnt( TTF_OpenFont( f.c_str(), 12 ) );
        if( !fnt ) {
            continue;
        }
        // TTF_FontFaces returns a long, so use that
        // NOLINTNEXTLINE(cata-no-long)
        long nfaces = 0;
        nfaces = TTF_FontFaces( fnt.get() );
        fnt.reset();

        // NOLINTNEXTLINE(cata-no-long)
        for( long i = 0; i < nfaces; i++ ) {
            const TTF_Font_Ptr fnt( TTF_OpenFontIndex( f.c_str(), 12, i ) );
            if( !fnt ) {
                continue;
            }

            // Add font family
            const char *fami = TTF_FontFaceFamilyName( fnt.get() );
            if( fami != nullptr ) {
                fout << fami;
            } else {
                continue;
            }

            // Add font style
            const char *style = TTF_FontFaceStyleName( fnt.get() );
            bool isbitmap = ends_with( f, ".fon" );
            if( style != nullptr && !isbitmap && strcasecmp( style, "Regular" ) != 0 ) {
                fout << " " << style;
            }
            if( isbitmap ) {
                std::set<std::string>::iterator it;
                it = bitmap_fonts.find( std::string( fami ) );
                if( it == bitmap_fonts.end() ) {
                    // First appearance of this font family
                    bitmap_fonts.insert( fami );
                } else { // Font in set. Add filename to family string
                    size_t start = f.find_last_of( "/\\" );
                    size_t end = f.find_last_of( '.' );
                    if( start != std::string::npos && end != std::string::npos ) {
                        fout << " [" << f.substr( start + 1, end - start - 1 ) + "]";
                    } else {
                        dbg( D_INFO ) << "Skipping wrong font file: \"" << f << "\"";
                    }
                }
            }
            fout << std::endl;

            // Add filename and font index
            fout << f << std::endl;
            fout << i << std::endl;

            // We use only 1 style in bitmap fonts.
            if( isbitmap ) {
                break;
            }
        }
    }
}

static void save_font_list()
{
    try {
        std::set<std::string> bitmap_fonts;
        write_to_file( PATH_INFO::fontlist(), [&]( std::ostream & fout ) {
            font_folder_list( fout, PATH_INFO::user_font(), bitmap_fonts );
            font_folder_list( fout, PATH_INFO::fontdir(), bitmap_fonts );
            font_folder_list( fout, "/usr/share/fonts", bitmap_fonts );
            font_folder_list( fout, "/usr/local/share/fonts", bitmap_fonts );
            char *home;
            if( ( home = getenv( "HOME" ) ) ) {
                std::string userfontdir = home;
                userfontdir += "/.fonts";
                font_folder_list( fout, userfontdir, bitmap_fonts );
            }

        } );
    } catch( const std::exception &err ) {
        // This is called during startup, the UI system may not be initialized (because that
        // needs the font file in order to load the font for it).
        dbg( D_ERROR ) << "Faied to create fontlist file \"" << PATH_INFO::fontlist() << "\": " <<
                       err.what();
    }
}

static cata::optional<std::string> find_system_font( const std::string &name, int &faceIndex )
{
    const std::string fontlist_path = PATH_INFO::fontlist();
    std::ifstream fin( fontlist_path.c_str() );
    if( !fin.is_open() ) {
        // Write out fontlist to the new location.
        save_font_list();
    }
    if( fin.is_open() ) {
        std::string fname;
        std::string fpath;
        std::string iline;
        while( getline( fin, fname ) && getline( fin, fpath ) && getline( fin, iline ) ) {
            if( 0 == strcasecmp( fname.c_str(), name.c_str() ) ) {
                faceIndex = atoi( iline.c_str() );
                return fpath;
            }
        }
    }

    return cata::nullopt;
}

// bitmap font size test
// return face index that has this size or below
static int test_face_size( const std::string &f, int size, int faceIndex )
{
    const TTF_Font_Ptr fnt( TTF_OpenFontIndex( f.c_str(), size, faceIndex ) );
    if( fnt ) {
        const char *style = TTF_FontFaceStyleName( fnt.get() );
        if( style != nullptr ) {
            int faces = TTF_FontFaces( fnt.get() );
            for( int i = faces - 1; i >= 0; i-- ) {
                const TTF_Font_Ptr tf( TTF_OpenFontIndex( f.c_str(), size, i ) );
                const char *ts = nullptr;
                if( tf ) {
                    if( nullptr != ( ts = TTF_FontFaceStyleName( tf.get() ) ) ) {
                        if( 0 == strcasecmp( ts, style ) && TTF_FontHeight( tf.get() ) <= size ) {
                            return i;
                        }
                    }
                }
            }
        }
    }

    return faceIndex;
}

// Calculates the new width of the window
int projected_window_width()
{
    return get_option<int>( "TERMINAL_X" ) * fontwidth;
}

// Calculates the new height of the window
int projected_window_height()
{
    return get_option<int>( "TERMINAL_Y" ) * fontheight;
}

static void init_term_size_and_scaling_factor()
{
    scaling_factor = 1;
    int terminal_x = get_option<int>( "TERMINAL_X" );
    int terminal_y = get_option<int>( "TERMINAL_Y" );

    if( get_option<std::string>( "SCALING_FACTOR" ) == "2" ) {
        scaling_factor = 2;
    } else if( get_option<std::string>( "SCALING_FACTOR" ) == "4" ) {
        scaling_factor = 4;
    }

    if( scaling_factor > 1 ) {

        int max_width, max_height;

        int current_display_id = std::stoi( get_option<std::string>( "DISPLAY" ) );
        SDL_DisplayMode current_display;

        if( SDL_GetDesktopDisplayMode( current_display_id, &current_display ) == 0 ) {
            if( get_option<std::string>( "FULLSCREEN" ) == "no" ) {

                // Make a maximized test window to determine maximum windowed size
                SDL_Window_Ptr test_window;
                test_window.reset( SDL_CreateWindow( "test_window",
                                                     SDL_WINDOWPOS_CENTERED_DISPLAY( current_display_id ),
                                                     SDL_WINDOWPOS_CENTERED_DISPLAY( current_display_id ),
                                                     FULL_SCREEN_WIDTH * fontwidth,
                                                     FULL_SCREEN_HEIGHT * fontheight,
                                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED
                                                   ) );

                SDL_GetWindowSize( test_window.get(), &max_width, &max_height );

                // If the video subsystem isn't reset the test window messes things up later
                test_window.reset();
                SDL_QuitSubSystem( SDL_INIT_VIDEO );
                SDL_InitSubSystem( SDL_INIT_VIDEO );

            } else {
                // For fullscreen or window borderless maximum size is the display size
                max_width = current_display.w;
                max_height = current_display.h;
            }
        } else {
            dbg( D_WARNING ) << "Failed to get current Display Mode, assuming infinite display size.";
            max_width = INT_MAX;
            max_height = INT_MAX;
        }

        if( terminal_x * fontwidth > max_width ||
            FULL_SCREEN_WIDTH * fontwidth * scaling_factor > max_width ) {
            if( FULL_SCREEN_WIDTH * fontwidth * scaling_factor > max_width ) {
                dbg( D_WARNING ) << "SCALING_FACTOR set too high for display size, resetting to 1";
                scaling_factor = 1;
                terminal_x = max_width / fontwidth;
                terminal_y = max_height / fontheight;
                get_options().get_option( "SCALING_FACTOR" ).setValue( "1" );
            } else {
                terminal_x = max_width / fontwidth;
            }
        }

        if( terminal_y * fontheight > max_height ||
            FULL_SCREEN_HEIGHT * fontheight * scaling_factor > max_height ) {
            if( FULL_SCREEN_HEIGHT * fontheight * scaling_factor > max_height ) {
                dbg( D_WARNING ) << "SCALING_FACTOR set too high for display size, resetting to 1";
                scaling_factor = 1;
                terminal_x = max_width / fontwidth;
                terminal_y = max_height / fontheight;
                get_options().get_option( "SCALING_FACTOR" ).setValue( "1" );
            } else {
                terminal_y = max_height / fontheight;
            }
        }

        terminal_x -= terminal_x % scaling_factor;
        terminal_y -= terminal_y % scaling_factor;

        terminal_x = std::max( FULL_SCREEN_WIDTH * scaling_factor, terminal_x );
        terminal_y = std::max( FULL_SCREEN_HEIGHT * scaling_factor, terminal_y );

        get_options().get_option( "TERMINAL_X" ).setValue(
            std::max( FULL_SCREEN_WIDTH * scaling_factor, terminal_x ) );
        get_options().get_option( "TERMINAL_Y" ).setValue(
            std::max( FULL_SCREEN_HEIGHT * scaling_factor, terminal_y ) );

        get_options().save();
    }
    TERMINAL_WIDTH = terminal_x / scaling_factor;
    TERMINAL_HEIGHT = terminal_y / scaling_factor;
}

//Basic Init, create the font, backbuffer, etc
void catacurses::init_interface()
{
    last_input = input_event();
    inputdelay = -1;

    InitSDL();

    get_options().init();
    get_options().load();

    font_loader fl;
    fl.load();
    fl.fontwidth = get_option<int>( "FONT_WIDTH" );
    fl.fontheight = get_option<int>( "FONT_HEIGHT" );
    fl.fontsize = get_option<int>( "FONT_SIZE" );
    fl.fontblending = get_option<bool>( "FONT_BLENDING" );
    fl.map_fontsize = get_option<int>( "MAP_FONT_SIZE" );
    fl.map_fontwidth = get_option<int>( "MAP_FONT_WIDTH" );
    fl.map_fontheight = get_option<int>( "MAP_FONT_HEIGHT" );
    fl.overmap_fontsize = get_option<int>( "OVERMAP_FONT_SIZE" );
    fl.overmap_fontwidth = get_option<int>( "OVERMAP_FONT_WIDTH" );
    fl.overmap_fontheight = get_option<int>( "OVERMAP_FONT_HEIGHT" );
    ::fontwidth = fl.fontwidth;
    ::fontheight = fl.fontheight;

    init_term_size_and_scaling_factor();

    WinCreate();

    dbg( D_INFO ) << "Initializing SDL Tiles context";
    tilecontext = std::make_unique<cata_tiles>( renderer );
    try {
        tilecontext->load_tileset( get_option<std::string>( "TILES" ), true );
    } catch( const std::exception &err ) {
        dbg( D_ERROR ) << "failed to check for tileset: " << err.what();
        // use_tiles is the cached value of the USE_TILES option.
        // most (all?) code refers to this to see if cata_tiles should be used.
        // Setting it to false disables this from getting used.
        use_tiles = false;
    }

    color_loader<SDL_Color>().load( windowsPalette );
    init_colors();

    // initialize sound set
    load_soundset();

    // Reset the font pointer
    font = std::make_unique<FontFallbackList>( fl.fontwidth, fl.fontheight,
            fl.typeface, fl.fontsize, fl.fontblending );
    map_font = std::make_unique<FontFallbackList>( fl.map_fontwidth, fl.map_fontheight,
               fl.map_typeface, fl.map_fontsize, fl.fontblending );
    overmap_font = std::make_unique<FontFallbackList>( fl.overmap_fontwidth, fl.overmap_fontheight,
                   fl.overmap_typeface, fl.overmap_fontsize, fl.fontblending );
    stdscr = newwin( get_terminal_height(), get_terminal_width(), point_zero );
    //newwin calls `new WINDOW`, and that will throw, but not return nullptr.

}

// This is supposed to be called from init.cpp, and only from there.
void load_tileset()
{
    if( !tilecontext || !use_tiles ) {
        return;
    }
    tilecontext->load_tileset( get_option<std::string>( "TILES" ) );
    tilecontext->do_tile_loading_report();
}

std::unique_ptr<CataFont> CataFont::load_font( const std::string &typeface, int fontsize, int fontwidth,
                                       int fontheight, const bool fontblending )
{
    if( ends_with( typeface, ".bmp" ) || ends_with( typeface, ".png" ) ) {
        // Seems to be an image file, not a font.
        // Try to load as bitmap font from user font dir, then from font dir.
        try {
            return std::unique_ptr<CataFont>( std::make_unique<BitmapFont>( fontwidth, fontheight,
                                          PATH_INFO::user_font() + typeface ) );
        } catch( std::exception &err ) {
            try {
                return std::unique_ptr<CataFont>( std::make_unique<BitmapFont>( fontwidth, fontheight,
                                              PATH_INFO::fontdir() + typeface ) );
            } catch( std::exception &err ) {
                dbg( D_ERROR ) << "Failed to load " << typeface << ": " << err.what();
                // Continue to load as truetype font
            }
        }
    }
    // Not loaded as bitmap font (or it failed), try to load as truetype
    try {
        return std::unique_ptr<CataFont>( std::make_unique<CachedTTFFont>( fontwidth, fontheight,
                                      typeface, fontsize, fontblending ) );
    } catch( std::exception &err ) {
        dbg( D_ERROR ) << "Failed to load " << typeface << ": " << err.what();
    }
    return nullptr;
}

//Ends the terminal, destroy everything
void catacurses::endwin()
{
    tilecontext.reset();
    font.reset();
    map_font.reset();
    overmap_font.reset();
    WinDestroy();
}

template<>
SDL_Color color_loader<SDL_Color>::from_rgb( const int r, const int g, const int b )
{
    SDL_Color result;
    result.b = b;       //Blue
    result.g = g;       //Green
    result.r = r;       //Red
    result.a = 0xFF;    // Opaque
    return result;
}

void input_manager::set_timeout( const int t )
{
    input_timeout = t;
    inputdelay = t;
}

// This is how we're actually going to handle input events, SDL getch
// is simply a wrapper around this.
input_event input_manager::get_input_event()
{
    previously_pressed_key = 0;
    // standards note: getch is sometimes required to call refresh
    // see, e.g., http://linux.die.net/man/3/getch
    // so although it's non-obvious, that refresh() call (and maybe InvalidateRect?) IS supposed to be there

    wrefresh( catacurses::stdscr );

    if( inputdelay < 0 ) {
        do {
            CheckMessages();
            if( last_input.type != CATA_INPUT_ERROR ) {
                break;
            }
            SDL_Delay( 1 );
        } while( last_input.type == CATA_INPUT_ERROR );
    } else if( inputdelay > 0 ) {
        uint32_t starttime = SDL_GetTicks();
        uint32_t endtime = 0;
        bool timedout = false;
        do {
            CheckMessages();
            endtime = SDL_GetTicks();
            if( last_input.type != CATA_INPUT_ERROR ) {
                break;
            }
            SDL_Delay( 1 );
            timedout = endtime >= starttime + inputdelay;
            if( timedout ) {
                last_input.type = CATA_INPUT_TIMEOUT;
            }
        } while( !timedout );
    } else {
        CheckMessages();
    }

    if( last_input.type == CATA_INPUT_MOUSE ) {
        SDL_GetMouseState( &last_input.mouse_pos.x, &last_input.mouse_pos.y );
    } else if( last_input.type == CATA_INPUT_KEYBOARD ) {
        previously_pressed_key = last_input.get_first_input();
    }
    return last_input;
}

bool gamepad_available()
{
    return joystick != nullptr;
}

void rescale_tileset( int size )
{
    tilecontext->set_draw_scale( size );
    game_ui::init_ui();
}

cata::optional<tripoint> input_context::get_coordinates( const catacurses::window &capture_win_ )
{
    if( !coordinate_input_received ) {
        return cata::nullopt;
    }

    cata_cursesport::WINDOW *const capture_win = ( capture_win_.get() ? capture_win_ :
            g->w_terrain ).get<cata_cursesport::WINDOW>();

    // this contains the font dimensions of the capture_win,
    // not necessarily the global standard font dimensions.
    int fw = fontwidth;
    int fh = fontheight;
    // tiles might have different dimensions than standard font
    if( use_tiles && capture_win == g->w_terrain ) {
        fw = tilecontext->get_tile_width();
        fh = tilecontext->get_tile_height();
        // add_msg( m_info, "tile map fw %d fh %d", fw, fh);
    } else if( map_font && capture_win == g->w_terrain ) {
        // map font (if any) might differ from standard font
        fw = map_font->fontwidth;
        fh = map_font->fontheight;
    } else if( overmap_font && capture_win == g->w_overmap ) {
        fw = overmap_font->fontwidth;
        fh = overmap_font->fontheight;
    }

    // multiplied by the user's specified scaling factor regardless of whether tiles are in use
    fw = fw * get_scaling_factor();
    fh = fh * get_scaling_factor();

    // Translate mouse coordinates to map coordinates based on tile size,
    // the window position is *always* in standard font dimensions!
    const point win_min( capture_win->pos.x * fontwidth, capture_win->pos.y * fontheight );
    // But the size of the window is in the font dimensions of the window.
    const point win_size( capture_win->width * fw, capture_win->height * fh );
    const point win_max = win_min + win_size;
    // add_msg( m_info, "win_ left %d top %d right %d bottom %d", win_left,win_top,win_right,win_bottom);
    // add_msg( m_info, "coordinate_ x %d y %d", coordinate_x, coordinate_y);
    // Check if click is within bounds of the window we care about
    const rectangle win_bounds( win_min, win_max );
    if( !win_bounds.contains_inclusive( coordinate ) ) {
        return cata::nullopt;
    }

    point view_offset;
    if( capture_win == g->w_terrain ) {
        view_offset = g->ter_view_p.xy();
    }

    const point screen_pos = coordinate - win_min;
    point p;
    if( tile_iso && use_tiles ) {
        const float win_mid_x = win_min.x + win_size.x / 2.0f;
        const float win_mid_y = -win_min.y + win_size.y / 2.0f;
        const int screen_col = round( ( screen_pos.x - win_mid_x ) / ( fw / 2.0 ) );
        const int screen_row = round( ( screen_pos.y - win_mid_y ) / ( fw / 4.0 ) );
        const point selected( ( screen_col - screen_row ) / 2, ( screen_row + screen_col ) / 2 );
        p = view_offset + selected;
    } else {
        const point selected( screen_pos.x / fw, screen_pos.y / fh );
        p = view_offset + selected - point( capture_win->width / 2, capture_win->height / 2 );
    }

    return tripoint( p, g->get_levz() );
}

int get_terminal_width()
{
    return TERMINAL_WIDTH;
}

int get_terminal_height()
{
    return TERMINAL_HEIGHT;
}

int get_scaling_factor()
{
    return scaling_factor;
}

BitmapFont::BitmapFont( const int w, const int h, const std::string &typeface_path )
    : CataFont( w, h )
{
    dbg( D_INFO ) << "Loading bitmap font [" + typeface_path + "].";
    SDL_Surface_Ptr asciiload = load_image( typeface_path.c_str() );
    assert( asciiload );
    if( asciiload->w * asciiload->h < ( fontwidth * fontheight * 256 ) ) {
        throw std::runtime_error( "bitmap for font is to small" );
    }
    Uint32 key = SDL_MapRGB( asciiload->format, 0xFF, 0, 0xFF );
    SDL_SetColorKey( asciiload.get(), SDL_TRUE, key );
    SDL_Surface_Ptr ascii_surf[std::tuple_size<decltype( ascii )>::value];
    ascii_surf[0].reset( SDL_ConvertSurface( asciiload.get(), format.get(), 0 ) );
    SDL_SetSurfaceRLE( ascii_surf[0].get(), 1 );
    asciiload.reset();

    for( size_t a = 1; a < std::tuple_size<decltype( ascii )>::value; ++a ) {
        ascii_surf[a].reset( SDL_ConvertSurface( ascii_surf[0].get(), format.get(), 0 ) );
        SDL_SetSurfaceRLE( ascii_surf[a].get(), 1 );
    }

    for( size_t a = 0; a < std::tuple_size<decltype( ascii )>::value - 1; ++a ) {
        SDL_LockSurface( ascii_surf[a].get() );
        int size = ascii_surf[a]->h * ascii_surf[a]->w;
        Uint32 *pixels = static_cast<Uint32 *>( ascii_surf[a]->pixels );
        Uint32 color = ( windowsPalette[a].r << 16 ) | ( windowsPalette[a].g << 8 ) | windowsPalette[a].b;
        for( int i = 0; i < size; i++ ) {
            if( pixels[i] == 0xFFFFFF ) {
                pixels[i] = color;
            }
        }
        SDL_UnlockSurface( ascii_surf[a].get() );
    }
    tilewidth = ascii_surf[0]->w / fontwidth;

    //convert ascii_surf to SDL_Texture
    for( size_t a = 0; a < std::tuple_size<decltype( ascii )>::value; ++a ) {
        ascii[a] = CreateTextureFromSurface( renderer, ascii_surf[a] );
    }
}

void BitmapFont::draw_ascii_lines( unsigned char line_id, int drawx, int drawy, int FG ) const
{
    BitmapFont *t = const_cast<BitmapFont *>( this );
    switch( line_id ) {
        // box bottom/top side (horizontal line)
        case LINE_OXOX_C:
            t->OutputChar( 0xcd, drawx, drawy, FG );
            break;
        // box left/right side (vertical line)
        case LINE_XOXO_C:
            t->OutputChar( 0xba, drawx, drawy, FG );
            break;
        // box top left
        case LINE_OXXO_C:
            t->OutputChar( 0xc9, drawx, drawy, FG );
            break;
        // box top right
        case LINE_OOXX_C:
            t->OutputChar( 0xbb, drawx, drawy, FG );
            break;
        // box bottom right
        case LINE_XOOX_C:
            t->OutputChar( 0xbc, drawx, drawy, FG );
            break;
        // box bottom left
        case LINE_XXOO_C:
            t->OutputChar( 0xc8, drawx, drawy, FG );
            break;
        // box bottom north T (left, right, up)
        case LINE_XXOX_C:
            t->OutputChar( 0xca, drawx, drawy, FG );
            break;
        // box bottom east T (up, right, down)
        case LINE_XXXO_C:
            t->OutputChar( 0xcc, drawx, drawy, FG );
            break;
        // box bottom south T (left, right, down)
        case LINE_OXXX_C:
            t->OutputChar( 0xcb, drawx, drawy, FG );
            break;
        // box X (left down up right)
        case LINE_XXXX_C:
            t->OutputChar( 0xce, drawx, drawy, FG );
            break;
        // box bottom east T (left, down, up)
        case LINE_XOXX_C:
            t->OutputChar( 0xb9, drawx, drawy, FG );
            break;
        default:
            break;
    }
}

CachedTTFFont::CachedTTFFont( const int w, const int h, std::string typeface, int fontsize,
                              const bool fontblending )
    : CataFont( w, h )
    , fontblending( fontblending )
{
    const std::string original_typeface = typeface;
    int faceIndex = 0;
    if( const cata::optional<std::string> sysfnt = find_system_font( typeface, faceIndex ) ) {
        typeface = *sysfnt;
        dbg( D_INFO ) << "Using font [" + typeface + "] found in the system.";
    }
    if( !file_exist( typeface ) ) {
        faceIndex = 0;
        typeface = PATH_INFO::user_font() + original_typeface + ".ttf";
        dbg( D_INFO ) << "Using compatible font [" + typeface + "] found in user font dir.";
    }
    //make fontdata compatible with wincurse
    if( !file_exist( typeface ) ) {
        faceIndex = 0;
        typeface = PATH_INFO::fontdir() + original_typeface + ".ttf";
        dbg( D_INFO ) << "Using compatible font [" + typeface + "] found in font dir.";
    }
    //different default font with wincurse
    if( !file_exist( typeface ) ) {
        faceIndex = 0;
        typeface = PATH_INFO::fontdir() + "unifont.ttf";
        dbg( D_INFO ) << "Using fallback font [" + typeface + "] found in font dir.";
    }
    dbg( D_INFO ) << "Loading truetype font [" + typeface + "].";
    if( fontsize <= 0 ) {
        fontsize = fontheight - 1;
    }
    // SDL_ttf handles bitmap fonts size incorrectly
    if( typeface.length() > 4 &&
        strcasecmp( typeface.substr( typeface.length() - 4 ).c_str(), ".fon" ) == 0 ) {
        faceIndex = test_face_size( typeface, fontsize, faceIndex );
    }
    font.reset( TTF_OpenFontIndex( typeface.c_str(), fontsize, faceIndex ) );
    if( !font ) {
        throw std::runtime_error( TTF_GetError() );
    }
    TTF_SetFontStyle( font.get(), TTF_STYLE_NORMAL );
}

FontFallbackList::FontFallbackList( const int w, const int h,
                                    const std::vector<std::string> &typefaces,
                                    const int fontsize, const bool fontblending )
    : CataFont( w, h )
{
    for( const std::string &typeface : typefaces ) {
        std::unique_ptr<CataFont> font = CataFont::load_font( typeface, fontsize, w, h, fontblending );
        if( !font ) {
            throw std::runtime_error( "Cannot load font " + typeface );
        }
        fonts.emplace_back( std::move( font ) );
    }
    if( fonts.empty() ) {
        throw std::runtime_error( "Typeface list is empty" );
    }
}

bool FontFallbackList::isGlyphProvided( const std::string & ) const
{
    return true;
}

void FontFallbackList::OutputChar( const std::string &ch, const int x, const int y,
                                   const unsigned char color, const float opacity )
{
    auto cached = glyph_font.find( ch );
    if( cached == glyph_font.end() ) {
        for( auto it = fonts.begin(); it != fonts.end(); ++it ) {
            if( std::next( it ) == fonts.end() || ( *it )->isGlyphProvided( ch ) ) {
                cached = glyph_font.emplace( ch, it ).first;
            }
        }
    }
    ( *cached->second )->OutputChar( ch, x, y, color, opacity );
}

static int map_font_width()
{
    if( use_tiles && tilecontext ) {
        return tilecontext->get_tile_width();
    }
    return ( map_font ? map_font.get() : font.get() )->fontwidth;
}

static int map_font_height()
{
    if( use_tiles && tilecontext ) {
        return tilecontext->get_tile_height();
    }
    return ( map_font ? map_font.get() : font.get() )->fontheight;
}

static int overmap_font_width()
{
    return ( overmap_font ? overmap_font.get() : font.get() )->fontwidth;
}

static int overmap_font_height()
{
    return ( overmap_font ? overmap_font.get() : font.get() )->fontheight;
}

void to_map_font_dim_width( int &w )
{
    w = ( w * fontwidth ) / map_font_width();
}

void to_map_font_dim_height( int &h )
{
    h = ( h * fontheight ) / map_font_height();
}

void to_map_font_dimension( int &w, int &h )
{
    to_map_font_dim_width( w );
    to_map_font_dim_height( h );
}

void from_map_font_dimension( int &w, int &h )
{
    w = ( w * map_font_width() + fontwidth - 1 ) / fontwidth;
    h = ( h * map_font_height() + fontheight - 1 ) / fontheight;
}

void to_overmap_font_dimension( int &w, int &h )
{
    w = ( w * fontwidth ) / overmap_font_width();
    h = ( h * fontheight ) / overmap_font_height();
}

bool is_draw_tiles_mode()
{
    return use_tiles;
}

/** Saves a screenshot of the current viewport, as a PNG file, to the given location.
* @param file_path: A full path to the file where the screenshot should be saved.
* @returns `true` if the screenshot generation was successful, `false` otherwise.
*/
bool save_screenshot( const std::string &file_path )
{
    // Note: the viewport is returned by SDL and we don't have to manage its lifetime.
    SDL_Rect viewport;

    // Get the viewport size (width and heigth of the screen)
    SDL_RenderGetViewport( renderer.get(), &viewport );

    // Create SDL_Surface with depth of 32 bits (note: using zeros for the RGB masks sets a default value, based on the depth; Alpha mask will be 0).
    SDL_Surface_Ptr surface = CreateRGBSurface( 0, viewport.w, viewport.h, 32, 0, 0, 0, 0 );

    // Get data from SDL_Renderer and save them into surface
    if( printErrorIf( SDL_RenderReadPixels( renderer.get(), nullptr, surface->format->format,
                                            surface->pixels, surface->pitch ) != 0,
                      "save_screenshot: cannot read data from SDL_Renderer." ) ) {
        return false;
    }

    // Save screenshot as PNG file
    if( printErrorIf( IMG_SavePNG( surface.get(), file_path.c_str() ) != 0,
                      std::string( "save_screenshot: cannot save screenshot file: " + file_path ).c_str() ) ) {
        return false;
    }

    return true;
}



