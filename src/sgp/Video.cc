#include "Cursor_Control.h"
#include "Debug.h"
#include "FPS.h"
#include "HImage.h"
#include "Local.h"
#include "Logger.h"
#include "RenderWorld.h"
#include "Render_Dirty.h"
#include "Types.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "Video.h"
#include "Visualizer.h"
#include "UILayout.h"
#include "Icon.h"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#define MAX_CURSOR_WIDTH  64
#define MAX_CURSOR_HEIGHT 64

#define MAX_DIRTY_REGIONS 128

#define OVERSAMPLING_SCALE 4


// Globals for mouse cursor
static UINT16 gusMouseCursorWidth;
static UINT16 gusMouseCursorHeight;
static INT16  gsMouseCursorXOffset;
static INT16  gsMouseCursorYOffset;
INT16 gsMouseSizeYModifier = 0; // This can increase the size of gusMouseCursorHeight so image data (ie, text) outside the normal height of the mouse cursor can be copied onto screen buffer

static SDL_Rect MouseBackground = { 0, 0, 0, 0 };

// Dirty rectangle management variables
static SDL_Rect DirtyRegions[MAX_DIRTY_REGIONS];
static UINT32   guiDirtyRegionCount;
static BOOLEAN  gfForceFullScreenRefresh;


static SDL_Rect DirtyRegionsEx[MAX_DIRTY_REGIONS];
static UINT32   guiDirtyRegionExCount;


static SDL_Surface* MouseCursor;
static SDL_Surface* FrameBuffer;
SDL_Renderer*  GameRenderer;
SDL_Window* g_game_window;

static SDL_Surface* ScreenBuffer;
static SDL_Texture* ScreenTexture;
static SDL_Texture* ScaledScreenTexture;
static Uint32       g_window_flags = 0;
static VideoScaleQuality ScaleQuality = VideoScaleQuality::LINEAR;
static std::chrono::steady_clock::duration TimeBetweenRefreshScreens;

static void DeletePrimaryVideoSurfaces(void);

// returns if desktop resolution larger game resolution
BOOLEAN IsDesktopLargeEnough()
{
	const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(0);
	if (dm)
	{
		if (dm->w < SCREEN_WIDTH || dm->h < SCREEN_HEIGHT)
		{
			return false;
		}
	}
	return true;
}

void VideoSetFullScreen(const BOOLEAN enable)
{
	SDL_SetWindowFullscreen(g_game_window, enable);
	SDL_SetWindowMouseGrab(g_game_window, enable);
}

void VideoToggleFullScreen(void)
{
	VideoSetFullScreen(!(SDL_GetWindowFlags(g_game_window) & SDL_WINDOW_FULLSCREEN));
}

void VideoSetBrightness(float brightness)
{
	if (brightness < 0) return;

	if (ScreenTexture)        SDL_SetTextureColorModFloat(ScreenTexture, brightness, brightness, brightness);
	if (ScaledScreenTexture)  SDL_SetTextureColorModFloat(ScaledScreenTexture, brightness, brightness, brightness);
}


static void GetRGBDistribution();


// area of the screen shown zoomed to fill the window, w == 0 if none
static SGPBox g_zoomed_area{};

// where the zoomed area ends up in render (logical) coordinates
static SDL_FRect ZoomedAreaDestination()
{
	float const scale = std::min(float(SCREEN_WIDTH) / g_zoomed_area.w, float(SCREEN_HEIGHT) / g_zoomed_area.h);
	float const w     = g_zoomed_area.w * scale;
	float const h     = g_zoomed_area.h * scale;
	return SDL_FRect{ (SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2, w, h };
}


void VideoSetZoomedArea(SGPBox const* const area)
{
	SGPBox const new_area = area ? *area : SGPBox{};
	if (new_area.x == g_zoomed_area.x && new_area.y == g_zoomed_area.y &&
		new_area.w == g_zoomed_area.w && new_area.h == g_zoomed_area.h) return;
	g_zoomed_area = new_area;
	InvalidateScreen();
}


void VideoRenderToGameCoordinates(float& x, float& y)
{
	if (g_zoomed_area.w == 0) return;
	SDL_FRect const dst = ZoomedAreaDestination();
	x = g_zoomed_area.x + (x - dst.x) * g_zoomed_area.w / dst.w;
	y = g_zoomed_area.y + (y - dst.y) * g_zoomed_area.h / dst.h;
}


void VideoGameToRenderCoordinates(float& x, float& y)
{
	if (g_zoomed_area.w == 0) return;
	SDL_FRect const dst = ZoomedAreaDestination();
	x = dst.x + (x - g_zoomed_area.x) * dst.w / g_zoomed_area.w;
	y = dst.y + (y - g_zoomed_area.y) * dst.h / g_zoomed_area.h;
}


void InitializeVideoManager(const VideoScaleQuality quality, const int32_t targetFPS)
{
	ScaleQuality = quality;
	g_window_flags |= SDL_WINDOW_RESIZABLE;

	g_game_window = SDL_CreateWindow(APPLICATION_NAME,
					SCREEN_WIDTH, SCREEN_HEIGHT,
					g_window_flags);

	if (!(g_window_flags & SDL_WINDOW_FULLSCREEN))
	{
		// A window of SCREEN_WIDTH x SCREEN_HEIGHT points is tiny on modern
		// (high DPI) displays. Open it as large as the usable desktop area
		// allows instead, keeping the aspect ratio. Pixel perfect scaling
		// only supports integer factors, so round down to one in that case.
		SDL_Rect usable;
		if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(g_game_window), &usable))
		{
			// leave room for the window decorations
			float const fit = std::min(usable.w * 0.95f / SCREEN_WIDTH, usable.h * 0.90f / SCREEN_HEIGHT);
			float const factor = quality == VideoScaleQuality::PERFECT ? std::floor(fit) : fit;
			if (factor > 1.0f)
			{
				SDL_SetWindowSize(g_game_window, int(SCREEN_WIDTH * factor), int(SCREEN_HEIGHT * factor));
				SDL_SetWindowPosition(g_game_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
			}
		}
	}

	GameRenderer = SDL_CreateRenderer(g_game_window, nullptr);
	SDL_SetRenderLogicalPresentation(GameRenderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	SurfaceUniquePtr windowIcon(SDL_CreateSurfaceFrom(
			gWindowIconData.width,
			gWindowIconData.height,
			SDL_PIXELFORMAT_ABGR8888,
			(void*)gWindowIconData.pixel_data,
			gWindowIconData.bytes_per_pixel*gWindowIconData.width
	));
	SDL_SetWindowIcon(g_game_window, windowIcon.get());


	ClippingRect.set(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	ScreenBuffer = SDL_CreateSurface(
					SCREEN_WIDTH,
					SCREEN_HEIGHT,
					SDL_PIXELFORMAT_RGB565
	);

	if (ScreenBuffer == NULL) {
		SLOGE("SDL_CreateRGBSurface for ScreenBuffer failed: {}\n", SDL_GetError());
	}

	ScreenTexture = SDL_CreateTexture(GameRenderer,
					SDL_PIXELFORMAT_RGB565,
					SDL_TEXTUREACCESS_STREAMING,
					SCREEN_WIDTH, SCREEN_HEIGHT);

	if (ScreenTexture == NULL) {
		SLOGE("SDL_CreateTexture for ScreenTexture failed: {}\n", SDL_GetError());
	}


	if (ScaleQuality == VideoScaleQuality::PERFECT)
	{
		if (!IsDesktopLargeEnough())
		{
			// Pixel-perfect mode cannot handle scaling down, and will
			// result in a empty black screen if the window size is
			// smaller than logical render resolution.
			throw std::runtime_error("Game resolution must not be larger than desktop size. "
				"Please reduce game resolution or choose another scaling mode.");
		}
		SDL_SetWindowMinimumSize(g_game_window, SCREEN_WIDTH, SCREEN_HEIGHT);
		SDL_SetTextureScaleMode(ScreenTexture, SDL_SCALEMODE_NEAREST);
		SDL_SetRenderLogicalPresentation(GameRenderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
	}
	else if (ScaleQuality == VideoScaleQuality::NEAR_PERFECT)
	{
		SDL_SetTextureScaleMode(ScreenTexture, SDL_SCALEMODE_NEAREST);
		ScaledScreenTexture = SDL_CreateTexture(GameRenderer,
			SDL_PIXELFORMAT_RGB565,
			SDL_TEXTUREACCESS_TARGET,
			SCREEN_WIDTH * OVERSAMPLING_SCALE, SCREEN_HEIGHT * OVERSAMPLING_SCALE);

		if (ScaledScreenTexture == NULL)
		{
			SLOGE("SDL_CreateTexture for ScaledScreenTexture failed: {}\n", SDL_GetError());
		}
	}
	else
	{
		SDL_SetTextureScaleMode(ScreenTexture, SDL_SCALEMODE_LINEAR);
	}

	FrameBuffer = SDL_CreateSurface(
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		SDL_PIXELFORMAT_RGB565
	);

	if (FrameBuffer == NULL)
	{
		SLOGE("SDL_CreateRGBSurface for FrameBuffer failed: {}\n", SDL_GetError());
	}

	MouseCursor = SDL_CreateSurface(
		MAX_CURSOR_WIDTH,
		MAX_CURSOR_HEIGHT,
		SDL_PIXELFORMAT_RGB565
	);
	SDL_SetSurfaceColorKey(MouseCursor, true, 0);

	if (MouseCursor == NULL)
	{
		SLOGE("SDL_CreateRGBSurface for MouseCursor failed: {}\n", SDL_GetError());
	}

	SDL_HideCursor();

	// Initialize state variables
	gfForceFullScreenRefresh = TRUE;

	// This function must be called to setup RGB information
	GetRGBDistribution();

	TimeBetweenRefreshScreens = std::chrono::microseconds{1'000'000} / targetFPS;
}


void ShutdownVideoManager(void)
{
	// ScreenBuffer SDL surface freed by its SGPVSurface wrapper.
	ScreenBuffer = nullptr;

	if (ScreenTexture != NULL) {
		SDL_DestroyTexture(ScreenTexture);
		ScreenTexture = NULL;
	}

	if (ScaledScreenTexture != NULL) {
		SDL_DestroyTexture(ScaledScreenTexture);
		ScaledScreenTexture = NULL;
	}

	if (GameRenderer != NULL) {
		SDL_DestroyRenderer(GameRenderer);
		GameRenderer = NULL;
	}

	if (g_game_window != NULL) {
		SDL_DestroyWindow(g_game_window);
		g_game_window = NULL;
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}


namespace {
void AddToGivenRegionsList(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom,
	decltype(DirtyRegions) & regions,
	decltype(guiDirtyRegionCount) & regionCount)
{
	if (gfForceFullScreenRefresh)
	{
		// There's no point in going on since we are forcing a full screen refresh
		return;
	}

	if (regionCount < MAX_DIRTY_REGIONS)
	{
		// Well we haven't broken the MAX_DIRTY_REGIONS limit yet, so we register the new region

		// DO SOME PREMIMARY CHECKS FOR VALID RECTS
		if (iLeft < 0) iLeft = 0;
		if (iTop  < 0) iTop  = 0;

		if (iRight  > SCREEN_WIDTH)  iRight  = SCREEN_WIDTH;
		if (iBottom > SCREEN_HEIGHT) iBottom = SCREEN_HEIGHT;

		if (iRight - iLeft <= 0) return;
		if (iBottom - iTop <= 0) return;

		auto & newRegion{ regions[regionCount] };

		newRegion.x = iLeft;
		newRegion.y = iTop;
		newRegion.w = iRight  - iLeft;
		newRegion.h = iBottom - iTop;
		++regionCount;
	}
	else
	{
		// The MAX_DIRTY_REGIONS limit has been exceeded. Therefore we arbitrarely invalidate the entire
		// screen and force a full screen refresh
		InvalidateScreen();
	}
}

void AddRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	AddToGivenRegionsList(iLeft, iTop, iRight, iBottom, DirtyRegionsEx, guiDirtyRegionExCount);
}
}

void InvalidateRegion(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	AddToGivenRegionsList(iLeft, iTop, iRight, iBottom, DirtyRegions, guiDirtyRegionCount);
}



void InvalidateRegionEx(INT32 iLeft, INT32 iTop, INT32 iRight, INT32 iBottom)
{
	if (gfForceFullScreenRefresh)
	{
		// There's no point in going on since we are forcing a full screen refresh
		return;
	}

	// Check if we are spanning the rectangle - if so slit it up!
	if (iTop <= gsVIEWPORT_WINDOW_END_Y && iBottom > gsVIEWPORT_WINDOW_END_Y)
	{
		// Add new top region
		AddRegionEx(iLeft, iTop, iRight, gsVIEWPORT_WINDOW_END_Y);

		// Add new bottom region
		AddRegionEx(iLeft, gsVIEWPORT_WINDOW_END_Y, iRight, iBottom);
	}
	else
	{
		AddRegionEx(iLeft, iTop, iRight, iBottom);
	}
}


void InvalidateScreen(void)
{
	guiDirtyRegionCount = 0;
	guiDirtyRegionExCount = 0;
	gfForceFullScreenRefresh = TRUE;
}


/* Clip an SDL Rect to the SDL_Surface. This was previously done automatically by SDL_BlitSurface */
static SDL_Rect ClipToSurface(SDL_Rect const& rect, SDL_Surface const* const surface)
{
	SDL_Rect const bounds{ 0, 0, surface->w, surface->h };
	SDL_Rect clipped;
	// On an empty intersection SDL leaves a rect with a negative extent behind.
	if (!SDL_GetRectIntersection(&rect, &bounds, &clipped)) return SDL_Rect{ 0, 0, 0, 0 };
	return clipped;
}


//#define SCROLL_TEST

static void ScrollJA2Background(INT16 sScrollXIncrement, INT16 sScrollYIncrement)
{
	SDL_Surface* Dest   = ScreenBuffer; // Back
	SDL_Rect     SrcRect;
	SDL_Rect     DstRect;
	SDL_Rect     StripRegions[2];
	UINT16       NumStrips = 0;

	int const width  = SCREEN_WIDTH;
	int const height = gsVIEWPORT_WINDOW_END_Y - gsVIEWPORT_WINDOW_START_Y;

	if (sScrollXIncrement < 0)
	{
		SrcRect.x = 0;
		SrcRect.w = width + sScrollXIncrement;
		DstRect.x = -sScrollXIncrement;
		StripRegions[0].x = gsVIEWPORT_START_X;
		StripRegions[0].y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[0].w = -sScrollXIncrement;
		StripRegions[0].h = height;
		++NumStrips;
	}
	else if (sScrollXIncrement > 0)
	{
		SrcRect.x = sScrollXIncrement;
		SrcRect.w = width - sScrollXIncrement;
		DstRect.x = 0;
		StripRegions[0].x = gsVIEWPORT_END_X - sScrollXIncrement;
		StripRegions[0].y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[0].w = sScrollXIncrement;
		StripRegions[0].h = height;
		++NumStrips;
	}
	else
	{
		SrcRect.x = 0;
		SrcRect.w = width;
		DstRect.x = 0;
	}

	if (sScrollYIncrement < 0)
	{
		SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
		SrcRect.h = height + sScrollYIncrement;
		DstRect.y = gsVIEWPORT_WINDOW_START_Y - sScrollYIncrement;
		StripRegions[NumStrips].x = DstRect.x;
		StripRegions[NumStrips].y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[NumStrips].w = SrcRect.w;
		StripRegions[NumStrips].h = -sScrollYIncrement;
		++NumStrips;
	}
	else if (sScrollYIncrement > 0)
	{
		SrcRect.y = gsVIEWPORT_WINDOW_START_Y + sScrollYIncrement;
		SrcRect.h = height - sScrollYIncrement;
		DstRect.y = gsVIEWPORT_WINDOW_START_Y;
		StripRegions[NumStrips].x = DstRect.x;
		StripRegions[NumStrips].y = gsVIEWPORT_WINDOW_END_Y - sScrollYIncrement;
		StripRegions[NumStrips].w = SrcRect.w;
		StripRegions[NumStrips].h = sScrollYIncrement;
		++NumStrips;
	}
	else
	{
		SrcRect.y = gsVIEWPORT_WINDOW_START_Y;
		SrcRect.h = height;
		DstRect.y = gsVIEWPORT_WINDOW_START_Y;
	}

#ifdef SCROLL_TEST
	SDL_FillRect(Dest, NULL, 0);
#endif

	SDL_BlitSurface(Dest, &SrcRect, Dest, &DstRect);

	for (UINT i = 0; i < NumStrips; i++)
	{
		INT16 const x = static_cast<INT16>(StripRegions[i].x);
		INT16 const y = static_cast<INT16>(StripRegions[i].y);
		INT16 const w = static_cast<INT16>(StripRegions[i].w);
		INT16 const h = static_cast<INT16>(StripRegions[i].h);
		for (int j = y; j < y + h; ++j)
		{
			std::fill_n(gpZBuffer + j * SCREEN_WIDTH + x, w, 0);
		}

		RenderStaticWorldRect(x, y, x + w, y + h, TRUE);
		SDL_BlitSurface(FrameBuffer, &StripRegions[i], Dest, &StripRegions[i]);
	}

	// RESTORE SHIFTED
	RestoreShiftedVideoOverlays(sScrollXIncrement, sScrollYIncrement);

	// SAVE NEW
	SaveVideoOverlaysArea(BACKBUFFER);

	// BLIT NEW
	ExecuteVideoOverlaysToAlternateBuffer(BACKBUFFER);
}

void RefreshScreen(void)
{
	// Not initialised yet or already shut down?
	if (!ScreenTexture) return;

	const BOOLEAN scrolling = (gsScrollXIncrement != 0 || gsScrollYIncrement != 0);

	SDL_BlitSurface(FrameBuffer, &MouseBackground, ScreenBuffer, &MouseBackground);

	// This variable will hold the union of all modified regions.
	struct rect : SDL_Rect {
		void operator+=(SDL_Rect const& r) { SDL_GetRectUnion(this, &r, this); }
	} ScreenTextureUpdateRect{ MouseBackground };

	if (gfForceFullScreenRefresh || guiDirtyRegionCount > 0 || guiDirtyRegionExCount > 0)
	{
		if (gfForceFullScreenRefresh)
		{
			SDL_BlitSurface(FrameBuffer, NULL, ScreenBuffer, NULL);
			ScreenTextureUpdateRect = { 0, 0, ScreenBuffer->w, ScreenBuffer->h };
		}
		else
		{
			for (UINT32 i = 0; i < guiDirtyRegionCount; i++)
			{
				ScreenTextureUpdateRect += DirtyRegions[i];
				SDL_BlitSurface(FrameBuffer, &DirtyRegions[i], ScreenBuffer, &DirtyRegions[i]);
			}

			for (UINT32 i = 0; i < guiDirtyRegionExCount; i++)
			{
				SDL_Rect* r = &DirtyRegionsEx[i];
				if (scrolling)
				{
					// Check if we are completely out of bounds
					if (r->y <= gsVIEWPORT_WINDOW_END_Y && r->y + r->h <= gsVIEWPORT_WINDOW_END_Y)
					{
						continue;
					}
				}
				ScreenTextureUpdateRect += *r;
				SDL_BlitSurface(FrameBuffer, r, ScreenBuffer, r);
			}
		}
		if (scrolling)
		{
			ScrollJA2Background(gsScrollXIncrement, gsScrollYIncrement);
			gsScrollXIncrement = 0;
			gsScrollYIncrement = 0;
			ScreenTextureUpdateRect += SDL_Rect{
				gsVIEWPORT_START_X, gsVIEWPORT_WINDOW_START_Y,
				gsVIEWPORT_END_X - gsVIEWPORT_START_X,
				gsVIEWPORT_WINDOW_END_Y - gsVIEWPORT_WINDOW_START_Y };
		}
		gfIgnoreScrollDueToCenterAdjust = FALSE;
	}

	auto const cursorPos{ GetCursorPos() };
	SDL_Rect src;
	src.x = 0;
	src.y = 0;
	src.w = gusMouseCursorWidth;
	// gsMouseSizeYModifier can push the height past the cursor surface
	src.h = std::min<int>(gusMouseCursorHeight + gsMouseSizeYModifier, MouseCursor->h);
	// The cursor hot spot offset can move this off screen on any side.
	SDL_Rect const dst{
		cursorPos.iX - gsMouseCursorXOffset,
		cursorPos.iY - gsMouseCursorYOffset,
		src.w, src.h };
	SDL_BlitSurface(MouseCursor, &src, ScreenBuffer, &dst);

	// The part that actually ended up on screen
	SDL_Rect const blitted{ ClipToSurface(dst, ScreenBuffer) };
	ScreenTextureUpdateRect += blitted;
	MouseBackground = blitted;

	SDL_Rect const UpdateRect{ ClipToSurface(ScreenTextureUpdateRect, ScreenBuffer) };
	if (UpdateRect.w > 0 && UpdateRect.h > 0)
	{
		uint8_t const * SrcPixels = static_cast<uint8_t *>(ScreenBuffer->pixels)
			+ UpdateRect.y * ScreenBuffer->pitch
			+ UpdateRect.x * SDL_GetPixelFormatDetails(ScreenBuffer->format)->bytes_per_pixel;
		SDL_UpdateTexture(ScreenTexture, &UpdateRect,
		                  SrcPixels, ScreenBuffer->pitch);
	}

	SDL_RenderClear(GameRenderer);

	// optionally only show a part of the screen, scaled up
	SDL_FRect zoom_src;
	SDL_FRect zoom_dst;
	SDL_FRect const* zsrc = nullptr;
	SDL_FRect const* zdst = nullptr;
	if (g_zoomed_area.w != 0)
	{
		zoom_dst = ZoomedAreaDestination();
		zdst     = &zoom_dst;
	}

	if (ScaleQuality == VideoScaleQuality::NEAR_PERFECT) {
		SDL_SetRenderTarget(GameRenderer, ScaledScreenTexture);
		SDL_RenderTexture(GameRenderer, ScreenTexture, nullptr, nullptr);

		SDL_SetRenderTarget(GameRenderer, nullptr);
		if (zdst)
		{ // the scaled texture is OVERSAMPLING_SCALE times the screen size
			zoom_src = SDL_FRect{ float(g_zoomed_area.x * OVERSAMPLING_SCALE), float(g_zoomed_area.y * OVERSAMPLING_SCALE),
				float(g_zoomed_area.w * OVERSAMPLING_SCALE), float(g_zoomed_area.h * OVERSAMPLING_SCALE) };
			zsrc = &zoom_src;
		}
		SDL_RenderTexture(GameRenderer, ScaledScreenTexture, zsrc, zdst);
	}
	else {
		if (zdst)
		{
			zoom_src = SDL_FRect{ float(g_zoomed_area.x), float(g_zoomed_area.y), float(g_zoomed_area.w), float(g_zoomed_area.h) };
			zsrc = &zoom_src;
		}
		SDL_RenderTexture(GameRenderer, ScreenTexture, zsrc, zdst);
	}

	Visualizer::Render(GameRenderer);

	FPS::RenderPresentPtr(GameRenderer);

	gfForceFullScreenRefresh = FALSE;
	guiDirtyRegionCount = 0;
	guiDirtyRegionExCount = 0;
}


// This is a semi-private function that is supposed to be called only
// by GameLoop(). This is why is has external linkage but is not
// declared in Video.h.
void RefreshScreenCapped()
{
	static std::chrono::steady_clock::time_point LastRefresh;

	auto const now{ std::chrono::steady_clock::now() };
	if (gsScrollXIncrement != 0 || gsScrollYIncrement != 0 ||
	    now - LastRefresh >= TimeBetweenRefreshScreens)
	{
		LastRefresh = now;
		RefreshScreen();
	}
}


static void GetRGBDistribution()
{
	auto f = *SDL_GetPixelFormatDetails(ScreenBuffer->format);

	UINT32          const  r = f.Rmask;
	UINT32          const  g = f.Gmask;
	UINT32          const  b = f.Bmask;

	/* Mask the highest bit of each component. This is used for alpha blending. */
	guiTranslucentMask = (r & r >> 1) | (g & g >> 1) | (b & b >> 1);

	gusRedMask   = static_cast<UINT16>(r);
	gusGreenMask = static_cast<UINT16>(g);
	gusBlueMask  = static_cast<UINT16>(b);

	gusRedShift   = f.Rshift - (8 - f.Rbits);
	gusGreenShift = f.Gshift - (8 - f.Gbits);
	gusBlueShift  = f.Bshift - (8 - f.Bbits);
}


void SetMouseCursorProperties(INT16 sOffsetX, INT16 sOffsetY, UINT16 usCursorHeight, UINT16 usCursorWidth)
{
	gsMouseCursorXOffset = sOffsetX;
	gsMouseCursorYOffset = sOffsetY;
	gusMouseCursorWidth  = usCursorWidth;
	gusMouseCursorHeight = usCursorHeight;
}


static void SetPrimaryVideoSurfaces(void)
{
	// Delete surfaces if they exist
	DeletePrimaryVideoSurfaces();

	g_back_buffer  = new SGPVSurface(ScreenBuffer);
	g_mouse_buffer = new SGPVSurface(MouseCursor);
	g_frame_buffer = new SGPVSurface(FrameBuffer);
}

static void DeletePrimaryVideoSurfaces(void)
{
	delete g_back_buffer;
	g_back_buffer = NULL;

	delete g_frame_buffer;
	g_frame_buffer = NULL;

	delete g_mouse_buffer;
	g_mouse_buffer = NULL;
}

SGPVSurface* gpVSurfaceHead = 0;

void InitializeVideoSurfaceManager(void)
{
	//Shouldn't be calling this if the video surface manager already exists.
	//Call shutdown first...
	Assert(gpVSurfaceHead == NULL);
	gpVSurfaceHead = NULL;

	// Create primary and backbuffer from globals
	SetPrimaryVideoSurfaces();
}


void ShutdownVideoSurfaceManager(void)
{
	// Delete primary viedeo surfaces
	DeletePrimaryVideoSurfaces();

	while (gpVSurfaceHead)
	{
		delete gpVSurfaceHead;
	}
}
