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


namespace
{
	// Box blur of one channel, in place, along rows (step 1) or columns (step w).
	void BoxBlur(std::vector<INT32>& c, INT32 const w, INT32 const h, INT32 const radius, bool const horizontal)
	{
		INT32 const len   = horizontal ? w : h;
		INT32 const lines = horizontal ? h : w;
		INT32 const step  = horizontal ? 1 : w;
		INT32 const next  = horizontal ? w : 1;
		std::vector<INT32> line(len);
		for (INT32 l = 0; l < lines; ++l)
		{
			INT32* const p = c.data() + l * next;
			for (INT32 i = 0; i < len; ++i) line[i] = p[i * step];
			// running sum over [i - radius, i + radius], edges clamped
			INT32 sum = 0;
			for (INT32 k = -radius; k <= radius; ++k) sum += line[std::clamp(k, 0, len - 1)];
			for (INT32 i = 0; i < len; ++i)
			{
				p[i * step] = sum / (2 * radius + 1);
				sum += line[std::min(i + radius + 1, len - 1)] - line[std::max(i - radius, 0)];
			}
		}
	}
}


void FillSideMarginsWithMirroredImage(SGPVSurface* const dst, SGPBox const& box)
{
	INT32 const w = dst->Width();
	INT32 const h = dst->Height();
	INT32 const left   = std::clamp<INT32>(box.x, 0, w);
	INT32 const right  = std::clamp<INT32>(box.x + box.w, left, w);
	INT32 const y0     = std::clamp<INT32>(box.y, 0, h);
	INT32 const y1     = std::clamp<INT32>(box.y + box.h, y0, h);
	if (dst->BPP() != 16 || right - left < 2 || y1 == y0) return;
	if (left == 0 && right == w) return;

	SGPVSurface::Lock l(dst);
	UINT16* const buf = l.Buffer<UINT16>();
	UINT32 const pitch = l.Pitch() / 2;

	INT32 const BLUR_RADIUS = 12; // two box blur passes per direction
	INT32 const FADE_WIDTH  = 32; // distance over which the copy goes from sharp to blurred

	// Mirror x (outside the image) back into the image columns [left, right - 1].
	auto const mirror = [&](INT32 const x) -> INT32
	{
		INT32 const span = right - left;
		INT32 d = (x < left ? left - 1 - x : x - right) % (2 * span); // 0 next to the image
		if (d >= span) d = 2 * span - 1 - d;
		return x < left ? left + d : right - 1 - d;
	};

	auto const fill = [&](INT32 const x_from, INT32 const x_to)
	{
		INT32 const mw = x_to - x_from;
		INT32 const mh = y1 - y0;
		std::vector<INT32> r(mw * mh), g(mw * mh), b(mw * mh);
		for (INT32 y = 0; y < mh; ++y)
		{
			UINT16 const* const row = buf + (y0 + y) * pitch;
			for (INT32 x = 0; x < mw; ++x)
			{
				UINT16 const p = row[mirror(x_from + x)];
				r[y * mw + x] = p >> 11;
				g[y * mw + x] = (p >> 5) & 0x3F;
				b[y * mw + x] = p & 0x1F;
			}
		}
		// keep the sharp copy, scaled up for precision
		std::vector<INT32> sr(r), sg(g), sb(b);
		for (std::vector<INT32>* c : { &r, &g, &b })
		{
			for (INT32& v : *c) v <<= 8;
			for (int pass = 0; pass < 2; ++pass)
			{
				BoxBlur(*c, mw, mh, BLUR_RADIUS, true);
				BoxBlur(*c, mw, mh, BLUR_RADIUS, false);
			}
		}
		INT32 const margin = mw;
		for (INT32 x = 0; x < mw; ++x)
		{
			INT32 const sx = x_from + x;
			INT32 const dist = sx < left ? left - sx : sx - right + 1; // 1 .. margin
			// blend factor sharp -> blurred (0 .. 256), brightness 100 % -> 45 % (256 .. 115)
			INT32 const t = std::min(256, 256 * dist / FADE_WIDTH);
			INT32 const f = 256 - 141 * dist / margin;
			for (INT32 y = 0; y < mh; ++y)
			{
				INT32 const i = y * mw + x;
				auto const mix = [&](INT32 const sharp, INT32 const blurred, INT32 const max) -> UINT32
				{
					INT32 const v = ((sharp << 8) * (256 - t) + blurred * t) >> 8; // << 8 precision
					return UINT32(std::min(max, (v * f) >> 16));
				};
				buf[(y0 + y) * pitch + sx] = UINT16(mix(sr[i], r[i], 31) << 11 | mix(sg[i], g[i], 63) << 5 | mix(sb[i], b[i], 31));
			}
		}
	};
	if (left > 0)  fill(0, left);
	if (right < w) fill(right, w);
}


PixelImage16 ReadPixels16(SGPVSurface* const src, SGPBox const* const box)
{
	SGPBox const b = box ? *box : SGPBox{ 0, 0, src->Width(), src->Height() };
	PixelImage16 img;
	img.w = b.w;
	img.h = b.h;
	img.px.resize(img.w * img.h);
	if (src->BPP() != 16) return img;
	SGPVSurface::Lock l(src);
	UINT16 const* const buf = l.Buffer<UINT16>();
	UINT32 const pitch = l.Pitch() / 2;
	for (INT32 y = 0; y < img.h; ++y)
	{
		for (INT32 x = 0; x < img.w; ++x) img.at(x, y) = buf[(b.y + y) * pitch + b.x + x];
	}
	return img;
}


void WritePixels16(SGPVSurface* const dst, PixelImage16 const& img, INT32 const x, INT32 const y)
{
	if (dst->BPP() != 16) return;
	SGPVSurface::Lock l(dst);
	UINT16* const buf = l.Buffer<UINT16>();
	UINT32 const pitch = l.Pitch() / 2;
	for (INT32 iy = std::max(0, -y); iy < img.h && y + iy < dst->Height(); ++iy)
	{
		for (INT32 ix = std::max(0, -x); ix < img.w && x + ix < dst->Width(); ++ix)
		{
			buf[(y + iy) * pitch + x + ix] = img.at(ix, iy);
		}
	}
}


// Mix two RGB 565 pixels, t = 0 .. 256 being the weight of b.
static UINT16 Mix565(UINT16 const a, UINT16 const b, INT32 const t)
{
	INT32 const r  = ((a >> 11)         * (256 - t) + (b >> 11)         * t) >> 8;
	INT32 const g  = (((a >> 5) & 0x3F) * (256 - t) + ((b >> 5) & 0x3F) * t) >> 8;
	INT32 const bl = ((a & 0x1F)        * (256 - t) + (b & 0x1F)        * t) >> 8;
	return UINT16(r << 11 | g << 5 | bl);
}


PixelImage16 ResizeSeamless(PixelImage16 const& img, INT32 const new_len, INT32 split, INT32 reach, bool const horizontal)
{
	INT32 const len   = horizontal ? img.w : img.h;
	INT32 const delta = new_len - len;
	split = std::clamp(split, 1, len - 1);
	reach = std::clamp(reach, 1, std::min(split, len - split));
	INT32 const FADE = std::min(32, std::max(2, reach)); // width of the cross-fades

	// reflect i (0, 1, 2 ...) back and forth over n pixels
	auto const reflect = [](INT32 i, INT32 const n) -> INT32
	{
		i %= 2 * n;
		return i < n ? i : 2 * n - 1 - i;
	};

	// For output position o along the axis: source positions a and b and the
	// weight of b (0 .. 256).
	struct Src { INT32 a, b, t; };
	std::vector<Src> map(std::max(new_len, 0));
	for (INT32 o = 0; o < new_len; ++o)
	{
		Src& s = map[o];
		if (delta >= 0)
		{
			// left of the inserted span the pixels mirror the ones before split,
			// right of it the ones after split; cross-fade in the middle
			INT32 const fromLeft  = split - 1 - reflect(o - split, reach);
			INT32 const fromRight = split + reflect(split + delta - 1 - o, reach);
			if (o < split)              s = { o, o, 0 };
			else if (o >= split + delta) s = { o - delta, o - delta, 0 };
			else
			{
				INT32 const mid = split + delta / 2;
				INT32 const t = std::clamp(128 + (o - mid) * 256 / FADE, 0, 256);
				s = { fromLeft, fromRight, t };
			}
		}
		else
		{
			// leave out -delta pixels after split; cross-fade around the join
			INT32 const left  = std::min(o, len - 1);
			INT32 const right = std::clamp(o - delta, 0, len - 1);
			INT32 const t = std::clamp(128 + (o - split) * 256 / FADE, 0, 256);
			s = { left, right, t };
		}
	}

	PixelImage16 out;
	out.w = horizontal ? new_len : img.w;
	out.h = horizontal ? img.h : new_len;
	out.px.resize(std::max(out.w * out.h, 0));
	for (INT32 y = 0; y < out.h; ++y)
	{
		for (INT32 x = 0; x < out.w; ++x)
		{
			Src const& s = map[horizontal ? x : y];
			UINT16 const a = horizontal ? img.at(s.a, y) : img.at(x, s.a);
			UINT16 const b = horizontal ? img.at(s.b, y) : img.at(x, s.b);
			out.at(x, y) = s.t == 0 ? a : s.t == 256 ? b : Mix565(a, b, s.t);
		}
	}
	return out;
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
