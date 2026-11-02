#include "Debug.h"
#include "HImage.h"
#include "Shading.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "UILayout.h"

#include "SDL3/SDL.h"
#include <string_theory/format>
#include <string_theory/string>

#include <algorithm>
#include <stdexcept>
#include <vector>

extern SGPVSurface* gpVSurfaceHead;

namespace {

// Helper class to set a SDL surface's clipping rectangle to the equivalent
// of JA2's global clipping rectangle. Restores the previous clipping
// rectangle when it is destroyed.
class ApplyClippingRect
{
	SDL_Surface * surface;
	SDL_Rect oldClipRect;

public:
	ApplyClippingRect(SDL_Surface * s) : surface(s)
	{
          SDL_GetSurfaceClipRect(s, &oldClipRect);

          SGPRect clipRect = GetClippingRect();
          SDL_Rect newClipRect{clipRect.iLeft, clipRect.iTop,
                               clipRect.iRight - clipRect.iLeft + 1,
                               clipRect.iBottom - clipRect.iTop + 1};
          SDL_SetSurfaceClipRect(s, &newClipRect);
	}

	~ApplyClippingRect()
	{
		SDL_SetSurfaceClipRect(surface, &oldClipRect);
	}
};
}

SGPVSurface::SGPVSurface(UINT16 const w, UINT16 const h, UINT8 const bpp) :
	p16BPPPalette(),
	next_(gpVSurfaceHead)
{
	Assert(w > 0 && h > 0);

	SDL_PixelFormat pixelFormat;
	switch (bpp)
	{
		case 8:
			pixelFormat = SDL_PIXELFORMAT_INDEX8;
			break;

		case 16:
		{
			pixelFormat = SDL_PIXELFORMAT_RGB565;
			break;
		}

		default:
			throw std::logic_error("Tried to create video surface with invalid bpp, must be 8 or 16.");
	}
	SDL_Surface * const s = SDL_CreateSurface(w, h, pixelFormat);
	if (!s) throw std::runtime_error("Failed to create SDL surface");
	surface_.reset(s);
	gpVSurfaceHead = this;
}


SGPVSurface::SGPVSurface(SDL_Surface* const s) :
	surface_(s),
	p16BPPPalette(),
	next_(gpVSurfaceHead)
{
	gpVSurfaceHead = this;
}


SGPVSurface::~SGPVSurface()
{
	for (SGPVSurface** anchor = &gpVSurfaceHead;; anchor = &(*anchor)->next_)
	{
		if (*anchor != this) continue;
		*anchor = next_;
		break;
	}

	if (p16BPPPalette) delete[] p16BPPPalette;
}


void SGPVSurface::SetPalette(const SGPPaletteEntry* const src_pal)
{
	// SDL's palettes are reference counted, so we can
	// a) safely insert the newly created palette into a surface without
	//    checking for an existing one first
	// b) destroy the palette after doing so
	auto * palette = SDL_CreatePalette(256);
	SDL_SetPaletteColors(palette, src_pal, 0, 256);
	SDL_SetSurfacePalette(surface_.get(), palette);
	SDL_DestroyPalette(palette);

	if (p16BPPPalette != NULL) delete[] p16BPPPalette;
	p16BPPPalette = Create16BPPPalette(src_pal);
}


void SGPVSurface::SetTransparency(const COLORVAL colour)
{
	Uint32 colour_key;
	switch (BPP())
	{
		case  8: colour_key = colour;                break;
		case 16: colour_key = Get16BPPColor(colour); break;

		default: abort(); // HACK000E
	}
	SDL_SetSurfaceColorKey(surface_.get(), true, colour_key);
}


void SGPVSurface::Fill(const UINT16 colour)
{
	SDL_FillSurfaceRect(surface_.get(), NULL, colour);
}


static void InternalShadowVideoSurfaceRect(SDL_Surface * dst, INT32 x1, INT32 y1, INT32 x2, INT32 y2, float shadeFactor)
{
	ApplyClippingRect acr{ dst };

	Uint8 modF = static_cast<Uint8>(255 * shadeFactor);
	SDL_SetSurfaceColorMod(dst, modF, modF, modF);

	SDL_Rect blitRect{ x1, y1, x2 - x1 + 1, y2 - y1 + 1 };
	SDL_BlitSurface(dst, &blitRect, dst, &blitRect);

	SDL_SetSurfaceColorMod(dst, 255, 255, 255);
}


void SGPVSurface::ShadowRect(INT32 const x1, INT32 const y1, INT32 const x2, INT32 const y2)
{
	InternalShadowVideoSurfaceRect(surface_.get(), x1, y1, x2, y2, GetShadeTablePercent());
}


void SGPVSurface::ShadowRectUsingLowPercentTable(INT32 const x1, INT32 const y1, INT32 const x2, INT32 const y2)
{
	// 0.8 is the factor that was used to compute the IntensityTable in vanilla.
	InternalShadowVideoSurfaceRect(surface_.get(), x1, y1, x2, y2, 0.80f);
}


SGPVSurface* AddVideoSurfaceFromFile(const char* const Filename)
{
	AutoSGPImage img(CreateImage(Filename, IMAGE_ALLIMAGEDATA));

	auto vs = std::make_unique<SGPVSurface>(img->usWidth, img->usHeight, img->ubBitDepth);

	// Copy palette from the SGPImage to the SGPVSurface if necessary.
	if (img->ubBitDepth == 8) vs->SetPalette(img->pPalette);

	auto && sdlSurface{ vs->GetSDLSurface() };
	auto const format{ sdlSurface.format };

	// Leave it to SDL to copy the pixel data from the image to the surface.
	SDL_ConvertPixels(img->usWidth, img->usHeight,
		format, &*img->pImageData, img->usWidth * img->ubBitDepth / 8,
		format, sdlSurface.pixels, sdlSurface.pitch);

	return vs.release();
}


namespace {

// One box of the median cut colour quantizer: a range of unique colours
struct ColorBox
{
	size_t begin, end;
	UINT8  minC[3], maxC[3];

	int LongestAxis() const
	{
		int axis = 0;
		for (int c = 1; c < 3; ++c)
		{
			if (maxC[c] - minC[c] > maxC[axis] - minC[axis]) axis = c;
		}
		return axis;
	}

	int Range() const
	{
		int const a = LongestAxis();
		return maxC[a] - minC[a];
	}
};

struct WeightedColor
{
	UINT8  c[3];
	UINT32 count;
	UINT16 value; // original 16 bpp colour
};

void ShrinkBox(ColorBox& box, std::vector<WeightedColor> const& colors)
{
	for (int c = 0; c < 3; ++c) { box.minC[c] = 255; box.maxC[c] = 0; }
	for (size_t i = box.begin; i != box.end; ++i)
	{
		for (int c = 0; c < 3; ++c)
		{
			box.minC[c] = std::min(box.minC[c], colors[i].c[c]);
			box.maxC[c] = std::max(box.maxC[c], colors[i].c[c]);
		}
	}
}

} // namespace


SGPVSurface* AddVideoSurfaceFromFileAs8BPP(const char* const Filename, UINT8 const scale)
{
	AutoSGPImage img(CreateImage(Filename, IMAGE_ALLIMAGEDATA));
	if (img->ubBitDepth == 8 && scale == 1)
	{
		// Nothing to convert, use the regular code path
		return AddVideoSurfaceFromFile(Filename);
	}
	if (img->ubBitDepth != 16 || scale == 0)
	{
		throw std::runtime_error(ST::format("{}: cannot convert {} bpp image to 8 bpp", Filename, img->ubBitDepth).to_std_string());
	}

	UINT16 const  w   = img->usWidth;
	UINT16 const  h   = img->usHeight;
	UINT16 const* src = reinterpret_cast<UINT16 const*>(&*img->pImageData);

	// Histogram of all RGB565 colours used by the image
	std::vector<UINT32> histogram(65536, 0);
	for (size_t i = 0; i != size_t(w) * h; ++i) ++histogram[src[i]];

	std::vector<WeightedColor> colors;
	for (UINT32 v = 0; v != histogram.size(); ++v)
	{
		if (histogram[v] == 0) continue;
		UINT32 const rgb = GetRGBColor(UINT16(v));
		colors.push_back({ { SGPGetRValue(rgb), SGPGetGValue(rgb), SGPGetBValue(rgb) }, histogram[v], UINT16(v) });
	}

	// Median cut: repeatedly split the box with the largest colour range
	std::vector<ColorBox> boxes;
	boxes.push_back({ 0, colors.size(), {}, {} });
	ShrinkBox(boxes[0], colors);
	while (boxes.size() < 256)
	{
		size_t best = boxes.size();
		for (size_t i = 0; i != boxes.size(); ++i)
		{
			if (boxes[i].end - boxes[i].begin < 2) continue;
			if (best == boxes.size() || boxes[i].Range() > boxes[best].Range()) best = i;
		}
		if (best == boxes.size()) break; // every box holds a single colour

		ColorBox& box = boxes[best];
		int const axis = box.LongestAxis();
		std::sort(colors.begin() + box.begin, colors.begin() + box.end,
			[axis](WeightedColor const& a, WeightedColor const& b) { return a.c[axis] < b.c[axis]; });

		// Split at the weighted median
		uint64_t total = 0;
		for (size_t i = box.begin; i != box.end; ++i) total += colors[i].count;
		uint64_t acc = 0;
		size_t split = box.begin + 1;
		for (size_t i = box.begin; i != box.end - 1; ++i)
		{
			acc += colors[i].count;
			split = i + 1;
			if (acc * 2 >= total) break;
		}

		ColorBox upper{ split, box.end, {}, {} };
		box.end = split;
		ShrinkBox(box, colors);
		ShrinkBox(upper, colors);
		boxes.push_back(upper);
	}

	// Palette entry of every box is its weighted average colour
	SGPPaletteEntry palette[256]{};
	std::vector<UINT8> lookup(65536, 0);
	for (size_t b = 0; b != boxes.size(); ++b)
	{
		uint64_t sum[3]{};
		uint64_t weight = 0;
		for (size_t i = boxes[b].begin; i != boxes[b].end; ++i)
		{
			for (int c = 0; c < 3; ++c) sum[c] += uint64_t(colors[i].c[c]) * colors[i].count;
			weight += colors[i].count;
		}
		palette[b].r = UINT8(sum[0] / weight);
		palette[b].g = UINT8(sum[1] / weight);
		palette[b].b = UINT8(sum[2] / weight);
		palette[b].a = 255;

		for (size_t i = boxes[b].begin; i != boxes[b].end; ++i)
		{
			lookup[colors[i].value] = UINT8(b);
		}
	}

	auto vs = std::make_unique<SGPVSurface>(UINT16(w * scale), UINT16(h * scale), 8);
	vs->SetPalette(palette);
	{
		SGPVSurface::Lock l(vs.get());
		UINT8* const dst   = l.Buffer<UINT8>();
		UINT32 const pitch = l.Pitch();
		for (UINT32 y = 0; y != UINT32(h) * scale; ++y)
		{
			UINT16 const* const srcRow = src + (y / scale) * w;
			UINT8*        const dstRow = dst + y * pitch;
			for (UINT32 x = 0; x != UINT32(w) * scale; ++x)
			{
				dstRow[x] = lookup[srcRow[x / scale]];
			}
		}
	}

	return vs.release();
}


void BltVideoSurfacePalettized(SGPVSurface* const dst, SGPVSurface* const src, INT32 const DestX, INT32 const DestY, SGPBox const* const src_rect)
{
	Assert(src->BPP() == 8);
	Assert(dst->BPP() == 16);

	INT32 sx = 0;
	INT32 sy = 0;
	INT32 w  = src->Width();
	INT32 h  = src->Height();
	if (src_rect)
	{
		sx = src_rect->x;
		sy = src_rect->y;
		w  = std::min<INT32>(src_rect->w, src->Width()  - sx);
		h  = std::min<INT32>(src_rect->h, src->Height() - sy);
	}

	// clip against the destination surface
	INT32 dx = DestX;
	INT32 dy = DestY;
	if (dx < 0) { sx -= dx; w += dx; dx = 0; }
	if (dy < 0) { sy -= dy; h += dy; dy = 0; }
	w = std::min<INT32>(w, dst->Width()  - dx);
	h = std::min<INT32>(h, dst->Height() - dy);
	if (w <= 0 || h <= 0) return;

	SGPVSurface::Lock lsrc(src);
	SGPVSurface::Lock ldst(dst);
	UINT16 const* const pal = src->p16BPPPalette;
	for (INT32 row = 0; row != h; ++row)
	{
		UINT8 const* s = lsrc.Buffer<UINT8>() + (sy + row) * lsrc.Pitch() + sx;
		UINT16*      d = reinterpret_cast<UINT16*>(ldst.Buffer<UINT8>() + (dy + row) * ldst.Pitch()) + dx;
		for (INT32 col = 0; col != w; ++col) d[col] = pal[s[col]];
	}
}


void BltVideoSurfaceHalf(SGPVSurface* const dst, SGPVSurface* const src, INT32 const DestX, INT32 const DestY, SGPBox const* const src_rect)
{
	SGPVSurface::Lock lsrc(src);
	SGPVSurface::Lock ldst(dst);
	UINT8*  const SrcBuf         = lsrc.Buffer<UINT8>();
	UINT32  const SrcPitchBYTES  = lsrc.Pitch();
	UINT16* const DestBuf        = ldst.Buffer<UINT16>();
	UINT32  const DestPitchBYTES = ldst.Pitch();
	Blt8BPPDataTo16BPPBufferHalf(DestBuf, DestPitchBYTES, src, SrcBuf, SrcPitchBYTES, DestX, DestY, src_rect);
}


void ColorFillVideoSurfaceArea(SGPVSurface* const dst, INT32 iDestX1, INT32 iDestY1, INT32 iDestX2, INT32 iDestY2, const UINT16 Color16BPP)
{
	SGPRect const Clip = GetClippingRect();

	if (iDestX1 < Clip.iLeft) iDestX1 = Clip.iLeft;
	if (iDestX1 > Clip.iRight) return;

	if (iDestX2 > Clip.iRight) iDestX2 = Clip.iRight;
	if (iDestX2 < Clip.iLeft) return;

	if (iDestY1 < Clip.iTop) iDestY1 = Clip.iTop;
	if (iDestY1 > Clip.iBottom) return;

	if (iDestY2 > Clip.iBottom) iDestY2 = Clip.iBottom;
	if (iDestY2 < Clip.iTop) return;

	if (iDestX2 <= iDestX1 || iDestY2 <= iDestY1) return;

	SDL_Rect Rect;
	Rect.x = iDestX1;
	Rect.y = iDestY1;
	Rect.w = iDestX2 - iDestX1;
	Rect.h = iDestY2 - iDestY1;
	SDL_FillSurfaceRect(dst->surface_.get(), &Rect, Color16BPP);
}


// Will drop down into user-defined blitter if 8->16 BPP blitting is being done
void BltVideoSurface(SGPVSurface* const dst, SGPVSurface* const src, INT32 const iDestX, INT32 const iDestY, SGPBox const* const src_box)
{
	Assert(dst);
	Assert(src);

	SDL_Rect* src_rect = 0;
	SDL_Rect  r;
	if (src_box)
	{
		r.x = src_box->x;
		r.y = src_box->y;
		r.w = src_box->w;
		r.h = src_box->h;
		src_rect = &r;
	}

	SDL_Rect dstrect;
	dstrect.x = iDestX;
	dstrect.y = iDestY;
	SDL_BlitSurface(src->surface_.get(), src_rect, dst->surface_.get(), &dstrect);
}


void BltStretchVideoSurface(SGPVSurface* const dst, SGPVSurface const* const src, SGPBox const* const src_rect, SGPBox const* const dst_rect)
{
	if (dst->BPP() != 16 || src->BPP() != 16) return;

	SDL_Surface const* const ssurface = src->surface_.get();
	SDL_Surface*       const dsurface = dst->surface_.get();

	const UINT32  s_pitch = ssurface->pitch >> 1;
	const UINT32  d_pitch = dsurface->pitch >> 1;
	UINT16 const* os      = (const UINT16*)ssurface->pixels + s_pitch * src_rect->y + src_rect->x;
	UINT16*       d       =       (UINT16*)dsurface->pixels + d_pitch * dst_rect->y + dst_rect->x;

	UINT const width  = dst_rect->w;
	UINT const height = dst_rect->h;
	UINT const dx     = src_rect->w;
	UINT const dy     = src_rect->h;
	UINT py = 0;
	if (ssurface->flags & SDL_SURFACE_PREALLOCATED)
	{
//		const UINT16 key = ssurface->format->colorkey;
		const UINT16 key = 0;
		for (UINT iy = 0; iy < height; ++iy)
		{
			const UINT16* s = os;
			UINT px = 0;
			for (UINT ix = 0; ix < width; ++ix)
			{
				if (*s != key) *d = *s;
				++d;
				px += dx;
				for (; px >= width; px -= width) ++s;
			}
			d += d_pitch - width;
			py += dy;
			for (; py >= height; py -= height) os += s_pitch;
		}
	}
	else
	{
		for (UINT iy = 0; iy < height; ++iy)
		{
			const UINT16* s = os;
			UINT px = 0;
			for (UINT ix = 0; ix < width; ++ix)
			{
				*d++ = *s;
				px += dx;
				for (; px >= width; px -= width) ++s;
			}
			d += d_pitch - width;
			py += dy;
			for (; py >= height; py -= height) os += s_pitch;
		}
	}
}


void BltVideoSurfaceOnce(SGPVSurface* const dst, const char* const filename, INT32 const x, INT32 const y)
{
	std::unique_ptr<SGPVSurface> src(AddVideoSurfaceFromFile(filename));
	BltVideoSurface(dst, src.get(), x, y, NULL);
}

/** Draw image on the video surface stretching the image if necessary. */
void BltVideoSurfaceOnceWithStretch(SGPVSurface* const dst, const char* const filename)
{
	std::unique_ptr<SGPVSurface> src(AddVideoSurfaceFromFile(filename));
	FillVideoSurfaceWithStretch(dst, src.get());
}

/** Fill video surface with another one with stretch. */
void FillVideoSurfaceWithStretch(SGPVSurface* const dst, SGPVSurface* const src)
{
	SGPBox srcRec;
	SGPBox dstRec;
	srcRec.set(0, 0, src->Width(), src->Height());
	dstRec.set(0, 0, dst->Width(), dst->Height());
	BltStretchVideoSurface(dst, src, &srcRec, &dstRec);
}

void BltVideoSurfaceSmoothScaled(SGPVSurface* const dst, SGPVSurface* const src, SGPBox const& dst_rect)
{
	SDL_Rect const r{ dst_rect.x, dst_rect.y, int(dst_rect.w), int(dst_rect.h) };
	SDL_BlitSurfaceScaled(src->surface_.get(), nullptr, dst->surface_.get(), &r, SDL_SCALEMODE_LINEAR);
}


SGPBox FitImageToScreen(UINT16 const w, UINT16 const h, UINT16 const max_w, UINT16 const max_h)
{
	float const scale = std::min({ 1.0f, float(max_w) / w, float(max_h) / h });
	UINT16 const dw = UINT16(w * scale);
	UINT16 const dh = UINT16(h * scale);
	return SGPBox{ UINT16((SCREEN_WIDTH - dw) / 2), UINT16((SCREEN_HEIGHT - dh) / 2), dw, dh };
}


void BltBackgroundImageOnce(SGPVSurface* const dst, const char* const filename, INT32 const std_x, INT32 const std_y)
{
	std::unique_ptr<SGPVSurface> src(AddVideoSurfaceFromFile(filename));
	// 640x480 is the size of the standard interface area
	if (src->Width() <= 640 && src->Height() <= 480)
	{
		BltVideoSurface(dst, src.get(), std_x, std_y, NULL);
		return;
	}
	BltVideoSurfaceSmoothScaled(dst, src.get(), FitImageToScreen(src->Width(), src->Height(), SCREEN_WIDTH, SCREEN_HEIGHT));
}


SGPPaletteEntry const * SGPVSurface::GetPalette() const
{
	auto * palette = SDL_GetSurfacePalette(surface_.get());
	return palette ? palette->colors : nullptr;
}

void DeleteVideoSurface(SGPVSurface const * vs)
{
	delete vs;
}
