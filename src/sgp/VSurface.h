#ifndef VSURFACE_H
#define VSURFACE_H

#include "SDL_helpers.h"
#include "Types.h"
#include <memory>
#include "SDL3/SDL.h"


#define BACKBUFFER   g_back_buffer
#define FRAME_BUFFER g_frame_buffer
#define MOUSE_BUFFER g_mouse_buffer

class SGPVSurface;

inline SGPVSurface* g_back_buffer;
inline SGPVSurface* g_frame_buffer;
inline SGPVSurface* g_mouse_buffer;

using SurfaceUniquePtr = std::unique_ptr<SDL_Surface, SDLDeleter>;


/** Utility wrapper around SDL_Surface. */
class SGPVSurface
{
	public:
		SGPVSurface(SDL_Surface*);
		SGPVSurface(UINT16 w, UINT16 h, UINT8 bpp);
		~SGPVSurface();

		UINT16 Width()  const { return surface_->w; }
		UINT16 Height() const { return surface_->h; }
		UINT8  BPP()    const { return SDL_GetPixelFormatDetails(surface_->format)->bits_per_pixel; }

		// Set palette, also sets 16BPP palette
		void SetPalette(const SGPPaletteEntry* src_pal);

		// Get the RGB palette entry values
		SGPPaletteEntry const * GetPalette() const;

		void SetTransparency(COLORVAL);

		/* Fill an entire surface with a colour */
		void Fill(UINT16 colour);

		void ShadowRect(INT32 x1, INT32 y1, INT32 x2, INT32 y2);
		void ShadowRectUsingLowPercentTable(INT32 x1, INT32 y1, INT32 x2, INT32 y2);

		/* Allow read access to the underlying SDL_Surface */
		SDL_Surface const& GetSDLSurface() const noexcept { return *surface_; }

		/* Fills an rectangular area with a specified color value. */
		friend void ColorFillVideoSurfaceArea(SGPVSurface*, INT32 iDestX1, INT32 iDestY1, INT32 iDestX2, INT32 iDestY2, UINT16 Color16BPP);

		// Blits a video Surface to another video Surface
		friend void BltVideoSurface(SGPVSurface* dst, SGPVSurface* src, INT32 iDestX, INT32 iDestY, SGPBox const* src_rect);

		/* This function will stretch the source image to the size of the dest rect.
		 * If the 2 images are not 16 Bpp, it returns false. */
		friend void BltStretchVideoSurface(SGPVSurface* dst, SGPVSurface const* src, SGPBox const* src_rect, SGPBox const* dst_rect);

		/* Blit the whole source surface into dst_rect using smooth (linear) scaling. */
		friend void BltVideoSurfaceSmoothScaled(SGPVSurface* dst, SGPVSurface* src, SGPBox const& dst_rect);

	private:
		SurfaceUniquePtr                           surface_;
	public:
		UINT16*                                    p16BPPPalette; // A 16BPP palette used for 8->16 blits
	private:
		SGPVSurface*                 next_;

		class LockBase
		{
			public:
				explicit LockBase(SDL_Surface* const s) : surface_(s) {}

				template<typename T> T* Buffer()
				{
					return static_cast<T*>(surface_->pixels);
				}

				UINT32 Pitch()
				{
					return surface_->pitch;
				}

			protected:
				SDL_Surface* surface_;
		};

	public:
		class Lock : public LockBase
		{
			public:
				explicit Lock(SGPVSurface* const vs) :
					LockBase(vs->surface_.get())
				{
					SDL_LockSurface(surface_);
				}

				~Lock()
				{
					SDL_UnlockSurface(surface_);
				}
		};

		class Lockable : public LockBase
		{
			public:
				explicit Lockable() : LockBase(0) {}

				~Lockable()
				{
					if (surface_) SDL_UnlockSurface(surface_);
				}

				void Lock(SGPVSurface* const vs)
				{
					if (surface_) SDL_UnlockSurface(surface_);
					surface_ = vs->surface_.get();
					if (surface_) SDL_LockSurface(surface_);
				}
		};
};

inline SGPVSurface* AddVideoSurface(UINT16 Width, UINT16 Height, UINT8 BitDepth)
{
	return new SGPVSurface(Width, Height, BitDepth);
}

SGPVSurface* AddVideoSurfaceFromFile(const char* Filename);

/* Load an image into an 8 bpp video surface. 16 bpp images are reduced to a
 * 256 colour palette and every pixel is scaled up by the given factor. Used to
 * load data of game versions which ship 16 bpp images where the engine needs a
 * palette (e.g. the strategic map of Jagged Alliance 2: Wildfire). */
SGPVSurface* AddVideoSurfaceFromFileAs8BPP(const char* Filename, UINT8 scale);

/* Blits a video surface in half size to another video surface.
 * If SrcRect is NULL the entire source surface is blitted.
 * Only blitting from 8bbp surfaces to 16bpp surfaces is supported. */
void BltVideoSurfaceHalf(SGPVSurface* dst, SGPVSurface* src, INT32 DestX, INT32 DestY, SGPBox const* src_rect);

/* Blit an 8 bpp surface onto a 16 bpp one at full size, using the source's
 * current 16 bpp palette (which may be swapped for shading). */
void BltVideoSurfacePalettized(SGPVSurface* dst, SGPVSurface* src, INT32 DestX, INT32 DestY, SGPBox const* src_rect);

// Deletes all data, including palettes
void DeleteVideoSurface(SGPVSurface const * vs);

void BltVideoSurfaceOnce(SGPVSurface* dst, const char* filename, INT32 x, INT32 y);

/** Draw image on the video surface stretching the image if necessary. */
void BltVideoSurfaceOnceWithStretch(SGPVSurface* const dst, const char* const filename);

/* Box of at most max_w x max_h with the aspect ratio of a w x h image, centered on the screen. */
SGPBox FitImageToScreen(UINT16 w, UINT16 h, UINT16 max_w, UINT16 max_h);

/* Draw a background image. Images larger than the standard 640x480 interface
 * area (e.g. the 1024x768 artwork of Jagged Alliance 2: Wildfire) are shown
 * at native size if the screen is large enough and scaled down otherwise,
 * instead of being cropped. */
void BltBackgroundImageOnce(SGPVSurface* dst, const char* filename, INT32 std_x, INT32 std_y);

/** Fill video surface with another one with stretch. */
void FillVideoSurfaceWithStretch(SGPVSurface* const dst, SGPVSurface* const src);

#endif
