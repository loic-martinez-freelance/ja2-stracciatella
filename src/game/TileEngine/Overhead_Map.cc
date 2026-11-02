#include "Overhead_Map.h"
#include "Button_System.h"
#include "ContentManager.h"
#include "Cursors.h"
#include "Directories.h"
#include "Faces.h"
#include "Font.h"
#include "Font_Control.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "GameLoop.h"
#include "GameMode.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "HImage.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "Interface_Items.h"
#include "Interface_Panels.h"
#include "Isometric_Utils.h"
#include "LightEffects.h"
#include "Line.h"
#include "Map_Information.h"
#include "MouseSystem.h"
#include "Object_Cache.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Radar_Screen.h"
#include "Render_Dirty.h"
#include "RenderWorld.h"
#include "SmokeEffects.h"
#include "Soldier_Control.h"
#include "Soldier_Init_List.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "Sys_Globals.h"
#include "SysUtil.h"
#include "Tactical_Placement_GUI.h"
#include "Tile_Surface.h"
#include "TileDat.h"
#include "TileDef.h"
#include "UILayout.h"
#include "Video.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "Local.h"
#include "World_Items.h"
#include "WorldDef.h"
#include <string_theory/string>
#include <algorithm>
#include <memory>

extern SOLDIERINITNODE *gpSelected;

// OK, these are values that are calculated in InitRenderParams( ) with normal view settings.
// These would be different if we change ANYTHING about the game worlkd map sizes...
#define NORMAL_MAP_SCREEN_WIDTH		3160
#define NORMAL_MAP_SCREEN_HEIGHT	1540
#define NORMAL_MAP_SCREEN_X		1580
#define NORMAL_MAP_SCREEN_BY		2400
#define NORMAL_MAP_SCREEN_TY		860

namespace {
cache_key_t const uiOVERMAP{ INTERFACEDIR "/map_bord.sti" };
cache_key_t const uiPERSONS{ INTERFACEDIR "/persons.sti" };


struct SMALL_TILE_SURF
{
	HVOBJECT vo;
};

struct SMALL_TILE_DB
{
	HVOBJECT	vo;
	UINT16		usSubIndex;
};
}

static SMALL_TILE_SURF gSmTileSurf[NUMBEROFTILETYPES];
static SMALL_TILE_DB   gSmTileDB[NUMBEROFTILES];
static TileSetID       gubSmTileNum                   = TILESET_INVALID;
static BOOLEAN         gfInOverheadMap = FALSE;
static MOUSE_REGION    OverheadRegion;
static MOUSE_REGION    OverheadBackgroundRegion;
BOOLEAN                gfOverheadMapDirty             = FALSE;
extern BOOLEAN		gfRadarCurrentGuyFlash;
static INT16           gsStartRestrictedX;
static INT16           gsStartRestrictedY;
static INT16           gsOveritemPoolGridNo           = NOWHERE;


static void CopyOverheadDBShadetablesFromTileset(void);


void InitNewOverheadDB(TileSetID const ubTilesetID)
{
	if (gubSmTileNum == ubTilesetID) return;
	TrashOverheadMap();

	for (UINT32 i = 0; i < NUMBEROFTILETYPES; ++i)
	{
		auto res = GetAdjustedTilesetResource(ubTilesetID, i, "t/");
		SGPVObject* vo;
		try
		{
			vo = AddVideoObjectFromFile(res.resourceFileName);
		}
		catch (std::exception &e)
		{
			SLOGD("{}", e.what());
			// Load one we know about
			vo = AddVideoObjectFromFile(GCM->getTilesetResourceName(GetDefaultTileset(), "t/grass.sti"));
		}

		gSmTileSurf[i].vo = vo;
	}

	// Create database
	UINT32 dbSize = 0;
	for (UINT32 i = 0; i < NUMBEROFTILETYPES; ++i)
	{
		SGPVObject* const vo = gSmTileSurf[i].vo;

		// Get number of regions and check for overflow
		UINT32 const NumRegions = std::min(vo->SubregionCount(), gNumTilesPerType[i]);

		UINT32 k = 0;
		for (; k < NumRegions; ++k)
		{
			gSmTileDB[dbSize].vo         = vo;
			gSmTileDB[dbSize].usSubIndex = k;
			++dbSize;
		}

		// Handle underflow
		for (; k < gNumTilesPerType[i]; ++k)
		{
			gSmTileDB[dbSize].vo         = vo;
			gSmTileDB[dbSize].usSubIndex = 0;
			++dbSize;
		}
	}

	gsStartRestrictedX = 0;
	gsStartRestrictedY = 0;

	// Calculate Scale factors because of restricted map scroll regions
	if (gMapInformation.ubRestrictedScrollID != 0)
	{
		INT16 sX1;
		INT16 sY1;
		INT16 sX2;
		INT16 sY2;
		CalculateRestrictedMapCoords(NORTH, &sX1, &sY1, &sX2, &gsStartRestrictedY, SCREEN_WIDTH, 320);
		CalculateRestrictedMapCoords(WEST,  &sX1, &sY1, &gsStartRestrictedX, &sY2, SCREEN_WIDTH, 320);
	}

	// Copy over shade tables from main tileset
	CopyOverheadDBShadetablesFromTileset();
	gubSmTileNum = ubTilesetID;
}


static ITEM_POOL const* GetClosestItemPool(INT16 const sweet_gridno, UINT8 const radius, INT8 const level)
{
	ITEM_POOL const* closest_item_pool = 0;
	INT32            lowest_range      = 999999;
	for (INT16 y = -radius; y <= radius; ++y)
	{
		INT32 const leftmost = (sweet_gridno + WORLD_COLS * y) / WORLD_COLS * WORLD_COLS;
		for (INT16 x = -radius; x <= radius; ++x)
		{
			INT16 const gridno = sweet_gridno + WORLD_COLS * y + x;
			if (gridno < 0        || WORLD_MAX             <= gridno) continue;
			if (gridno < leftmost || leftmost + WORLD_COLS <= gridno) continue;

			ITEM_POOL const* item_pool = GetItemPool(gridno, level);
			if (!item_pool) continue;

			INT32 const range = GetRangeInCellCoordsFromGridNoDiff(sweet_gridno, gridno);
			if (lowest_range <= range) continue;

			lowest_range      = range;
			closest_item_pool = item_pool;
		}
	}
	return closest_item_pool;
}


static SOLDIERTYPE* GetClosestMercInOverheadMap(INT16 const sweet_gridno, UINT8 const radius)
{
	SOLDIERTYPE* res          = 0;
	INT32        lowest_range = 999999;
	for (INT16 y = -radius; y <= radius; ++y)
	{
		INT32 const leftmost = (sweet_gridno + WORLD_COLS * y) / WORLD_COLS * WORLD_COLS;
		for (INT16 x = -radius; x <= radius; ++x)
		{
			INT16 const gridno = sweet_gridno + WORLD_COLS * y + x;
			if (gridno  < 0       || WORLD_MAX             <= gridno) continue;
			if (gridno < leftmost || leftmost + WORLD_COLS <= gridno) continue;

			// Go on sweet stop
			LEVELNODE const* const l = gpWorldLevelData[gridno].pMercHead;
			if (!l) continue;
			SOLDIERTYPE* const s = l->pSoldier;
			if (!l || s->bVisible == -1) continue;

			INT32 const range = GetRangeInCellCoordsFromGridNoDiff(sweet_gridno, gridno);
			if (lowest_range <= range) continue;

			lowest_range = range;
			res          = s;
		}
	}
	return res;
}


static INT16 GetOffsetLandHeight(INT32 const gridno)
{
	return gpWorldLevelData[gridno].sHeight;
}


// Wildfire: the 640x320 map is rendered at STD_SCREEN_X/Y as usual and then
// shown scaled up to fill the window of the (widened) frame.
static bool OverheadMapIsScaled()
{
	return g_ui.m_wildfireTacticalPanels && !gfEditMode;
}


// where the map is rendered
static SGPBox OverheadMapRenderBox()
{
	return SGPBox{ UINT16(STD_SCREEN_X), UINT16(STD_SCREEN_Y), 640, 320 };
}


// where the map is shown
static SGPBox OverheadMapDisplayBox()
{
	if (!OverheadMapIsScaled()) return OverheadMapRenderBox();
	INT32 const wx = OVERHEAD_FRAME_X + 4;
	INT32 const wy = OVERHEAD_FRAME_Y + 4;
	INT32 const ww = OVERHEAD_FRAME_WIDTH - 8;
	INT32 const wh = OVERHEAD_PANEL_Y - wy;
	float const scale = std::max(1.0f, std::min(ww / 640.0f, wh / 320.0f));
	UINT16 const w = UINT16(640 * scale);
	UINT16 const h = UINT16(320 * scale);
	return SGPBox{ UINT16(wx + (ww - w) / 2), UINT16(wy + (wh - h) / 2), w, h };
}


// render coordinates -> screen coordinates
static void OverheadMapToScreen(INT16& x, INT16& y)
{
	SGPBox const r = OverheadMapRenderBox();
	SGPBox const d = OverheadMapDisplayBox();
	x = INT16(d.x + (x - r.x) * d.w / r.w);
	y = INT16(d.y + (y - r.y) * d.h / r.h);
}


void GetOverheadMapMousePos(INT16* const x, INT16* const y)
{
	SGPBox const r = OverheadMapRenderBox();
	SGPBox const d = OverheadMapDisplayBox();
	*x = INT16(r.x + (gusMouseXPos - d.x) * r.w / d.w);
	*y = INT16(r.y + (gusMouseYPos - d.y) * r.h / d.h);
}


// The part of the frame buffer the map is rendered to is restored from the
// save buffer every frame, so that the overlays drawn there each frame can
// be scaled up along with the map.
static void RestoreOverheadMapRenderArea()
{
	if (!OverheadMapIsScaled()) return;
	SGPBox const r = OverheadMapRenderBox();
	BltVideoSurface(FRAME_BUFFER, guiSAVEBUFFER, r.x, r.y, &r);
}


static void ShowOverheadMapScaled()
{
	if (!OverheadMapIsScaled()) return;
	static std::unique_ptr<SGPVSurface> map;
	if (!map) map = std::make_unique<SGPVSurface>(640, 320, PIXEL_DEPTH);
	SGPBox const r = OverheadMapRenderBox();
	SGPBox const d = OverheadMapDisplayBox();
	BltVideoSurface(map.get(), FRAME_BUFFER, 0, 0, &r);
	BltVideoSurfaceSmoothScaled(FRAME_BUFFER, map.get(), d);
	InvalidateRegion(d.x, d.y, d.x + d.w, d.y + d.h);
}


// Wildfire's frame (map_bord.sti) widened to the screen width: the 4 pixel
// border around the map window and the bar below it.
static void RenderOverheadMapFrame()
{
	if (!OverheadMapIsScaled() || OVERHEAD_FRAME_WIDTH == 1024)
	{
		BltVideoObject(FRAME_BUFFER, uiOVERMAP, 0, OVERHEAD_FRAME_X, OVERHEAD_FRAME_Y);
		return;
	}
	static PixelImage16 frame;
	if (frame.w != OVERHEAD_FRAME_WIDTH)
	{
		SGPVObject const* const vo = GetVObject(uiOVERMAP);
		ETRLEObject const& e = vo->SubregionProperties(0);
		SGPVSurface tmp(e.usWidth, e.usHeight, PIXEL_DEPTH);
		BltVideoObject(&tmp, vo, 0, -e.sOffsetX, -e.sOffsetY);
		frame = ResizeSeamless(ReadPixels16(&tmp), OVERHEAD_FRAME_WIDTH, e.usWidth / 2, e.usWidth / 2 - 100, true);
	}
	// everything but the map window
	INT32 const window_bottom = OVERHEAD_PANEL_Y - OVERHEAD_FRAME_Y;
	PixelImage16 part;
	auto const write = [&](INT32 const x, INT32 const y, INT32 const w, INT32 const h)
	{
		part.w = w;
		part.h = h;
		part.px.resize(w * h);
		for (INT32 iy = 0; iy < h; ++iy)
		{
			for (INT32 ix = 0; ix < w; ++ix) part.at(ix, iy) = frame.at(x + ix, y + iy);
		}
		WritePixels16(FRAME_BUFFER, part, OVERHEAD_FRAME_X + x, OVERHEAD_FRAME_Y + y);
	};
	write(0, 0, frame.w, 4);
	write(0, 4, 4, window_bottom - 4);
	write(frame.w - 4, 4, 4, window_bottom - 4);
	write(0, window_bottom, frame.w, frame.h - window_bottom);
}


static void GetOverheadScreenXYFromGridNo(INT16 const gridno, INT16* const out_x, INT16* const out_y)
{
	GetAbsoluteScreenXYFromMapPos(gridno, out_x, out_y);
	INT16 x = *out_x / 5;
	INT16 y = *out_y / 5;

	x += gsStartRestrictedX + 5;
	y += gsStartRestrictedY + 5;

	y -= GetOffsetLandHeight(gridno) / 5;
	y += gsRenderHeight / 5;

	*out_x = x;
	*out_y = y;
}


static void DisplayMercNameInOverhead(SOLDIERTYPE const& s)
{
	// Get Screen position of guy
	INT16 x;
	INT16 y;
	GetOverheadScreenXYFromGridNo(s.sGridNo, &x, &y);

	x += STD_SCREEN_X;
	y += STD_SCREEN_Y;

	y -= s.sHeightAdjustment / 5 + 13;
	OverheadMapToScreen(x, y);

	INT16 sX;
	INT16 sY;
	SetFontAttributes(TINYFONT1, FONT_MCOLOR_WHITE);
	FindFontCenterCoordinates(x, y, 1, 1, s.name, TINYFONT1, &sX, &sY);
	GDirtyPrint(sX, sY, s.name);
}


static GridNo GetOverheadMouseGridNoForFullSoldiersGridNo(void);
static void   HandleOverheadUI(void);
static void   RenderOverheadOverlays(void);


void HandleOverheadMap(void)
{
	gfInOverheadMap      = TRUE;
	gsOveritemPoolGridNo = NOWHERE;

	InitNewOverheadDB(giCurrentTilesetID);

	RestoreBackgroundRects();

	// clear for broken saves before TrashWorld took care of this
	if (!gfEditMode && gfTacticalPlacementGUIActive)
	{
		DecaySmokeEffects(GetWorldTotalSeconds(), false);
		DecayLightEffects(GetWorldTotalSeconds(), false);
	}

	RenderOverheadMap(0, WORLD_COLS / 2, STD_SCREEN_X, STD_SCREEN_Y, STD_SCREEN_X + 640, STD_SCREEN_Y + 320, FALSE);
	RestoreOverheadMapRenderArea();

	HandleTalkingAutoFaces();

	if (!gfEditMode)
	{
		if (gfTacticalPlacementGUIActive)
		{
			TacticalPlacementHandle();
			if (!gfTacticalPlacementGUIActive) return;
		}
		else
		{
			HandleOverheadUI();

			if (!gfInOverheadMap) return;
			RenderTacticalInterface();
			RenderRadarScreen();
			RenderClock();
			RenderTownIDString();

			HandleAutoFaces();
		}
	}

	// item pools under the mouse; their lists are drawn after the map has been
	// scaled up, as they are not part of it
	ITEM_POOL const* ground_pool = NULL;
	INT8             ground_z    = 0;
	ITEM_POOL const* roof_pool   = NULL;
	if (!gfEditMode && !gfTacticalPlacementGUIActive)
	{
		HandleAnyMercInSquadHasCompatibleStuff(NULL);

		INT16 const usMapPos = GetOverheadMouseGridNo();
		if (usMapPos != NOWHERE)
		{
			const ITEM_POOL* pItemPool;

			// ATE: Find the closest item pool within 5 tiles....
			pItemPool = GetClosestItemPool(usMapPos, 1, 0);
			if (pItemPool != NULL)
			{
				const STRUCTURE* const structure = FindStructure(usMapPos, STRUCTURE_HASITEMONTOP | STRUCTURE_OPENABLE);
				INT8             const bZLevel   = GetZLevelOfItemPoolGivenStructure(usMapPos, 0, structure);
				if (AnyItemsVisibleOnLevel(pItemPool, bZLevel))
				{
					ground_pool = pItemPool;
					ground_z    = bZLevel;
					gsOveritemPoolGridNo = GetWorldItem(pItemPool->iItemIndex).sGridNo;
				}
			}

			pItemPool = GetClosestItemPool(usMapPos, 1, 1);
			if (pItemPool != NULL)
			{
				const INT8 bZLevel = 0;
				if (AnyItemsVisibleOnLevel(pItemPool, bZLevel))
				{
					roof_pool = pItemPool;
					gsOveritemPoolGridNo = GetWorldItem(pItemPool->iItemIndex).sGridNo;
				}
			}
		}
	}

	RenderOverheadOverlays();
	ShowOverheadMapScaled();

	if (ground_pool) DrawItemPoolList(ground_pool, ground_z, gusMouseXPos, gusMouseYPos);
	if (roof_pool)   DrawItemPoolList(roof_pool,   0,        gusMouseXPos, gusMouseYPos - 5);

	if (!gfEditMode && !gfTacticalPlacementGUIActive)
	{
		const SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel != NULL) DisplayMercNameInOverhead(*sel);

		gSelectedGuy = NULL;
		INT16 const usMapPos = GetOverheadMouseGridNoForFullSoldiersGridNo();
		if (usMapPos != NOWHERE)
		{
			SOLDIERTYPE* const s = GetClosestMercInOverheadMap(usMapPos, 1);
			if (s != NULL)
			{
				if (s->bTeam == OUR_TEAM) gSelectedGuy = s;
				DisplayMercNameInOverhead(*s);
			}
		}
	}

	RenderButtons();
	SaveBackgroundRects();
	RenderFastHelp();
	fInterfacePanelDirty = DIRTYLEVEL0;
}


BOOLEAN InOverheadMap( )
{
	return( gfInOverheadMap );
}


static void ClickOverheadRegionCallbackPrimary(MOUSE_REGION* reg, UINT32 reason);
static void ClickOverheadRegionCallbackSecondary(MOUSE_REGION* reg, UINT32 reason);


void GoIntoOverheadMap( )
{
	gfInOverheadMap = TRUE;

	MSYS_DefineRegion(&OverheadBackgroundRegion, OVERHEAD_FRAME_X, OVERHEAD_FRAME_Y, OVERHEAD_FRAME_X + OVERHEAD_FRAME_WIDTH, OVERHEAD_FRAME_Y + OVERHEAD_FRAME_HEIGHT,MSYS_PRIORITY_HIGH, CURSOR_NORMAL, MSYS_NO_CALLBACK, MSYS_NO_CALLBACK);

	SGPBox const map = OverheadMapDisplayBox();
	MSYS_DefineRegion(&OverheadRegion, map.x, map.y, map.x + map.w, map.y + map.h, MSYS_PRIORITY_HIGH, CURSOR_NORMAL, MSYS_NO_CALLBACK, MouseCallbackPrimarySecondary(ClickOverheadRegionCallbackPrimary, ClickOverheadRegionCallbackSecondary));

	// Add shades to persons....
	SGPVObject*            const vo  = GetVObject(uiPERSONS);
	SGPPaletteEntry const* const pal = vo->Palette();
	vo->pShades[0] = Create16BPPPaletteShaded(pal, 256, 256, 256, FALSE);
	vo->pShades[1] = Create16BPPPaletteShaded(pal, 310, 310, 310, FALSE);
	vo->pShades[2] = Create16BPPPaletteShaded(pal,   0,   0,   0, FALSE);

	gfOverheadMapDirty = TRUE;

	if( !gfEditMode )
	{
		// Make sure we are in team panel mode...
		SetNewPanel(0);
		fInterfacePanelDirty = DIRTYLEVEL2;

		// Disable tactical buttons......
		if( !gfEnterTacticalPlacementGUI )
		{
			// Handle switch of panel....
			HandleTacticalPanelSwitch( );
			DisableTacticalTeamPanelButtons( TRUE );
		}

		EmptyBackgroundRects( );
	}

}


static void HandleOverheadUI(void)
{
	InputAtom a;
	while (DequeueSpecificEvent(&a, KEYBOARD_EVENTS))
	{
		if (a.usEvent == KEY_DOWN)
		{
			switch (a.usParam)
			{
				case SDLK_ESCAPE:
				case SDLK_INSERT:
					KillOverheadMap();
					break;

				case 'x':
					if (a.usKeyState & ALT_DOWN)
					{
						HandleShortCutExitState();
					}
					break;
			}
		}
	}
}


void KillOverheadMap()
{
	gfInOverheadMap = FALSE;
	SetRenderFlags( RENDER_FLAG_FULL );
	RenderWorld( );

	MSYS_RemoveRegion(&OverheadRegion );
	MSYS_RemoveRegion(&OverheadBackgroundRegion );

	RemoveVObject(uiOVERMAP);
	RemoveVObject(uiPERSONS);

	HandleTacticalPanelSwitch( );
	DisableTacticalTeamPanelButtons( FALSE );

}


static INT16 GetModifiedOffsetLandHeight(INT32 const gridno)
{
	INT16 const h     = GetOffsetLandHeight(gridno);
	INT16 const mod_h = (h / 80 - 1) * 80;
	return mod_h < 0 ? 0 : mod_h;
}


void RenderOverheadMap(INT16 const sStartPointX_M, INT16 const sStartPointY_M, INT16 const sStartPointX_S, INT16 const sStartPointY_S, INT16 const sEndXS, INT16 const sEndYS, BOOLEAN const fFromMapUtility)
{
	if (!gfOverheadMapDirty) return;

	// Black out
	if (!fFromMapUtility && g_ui.m_wildfireTacticalPanels)
	{ // Wildfire's frame window is larger than the map
		ColorFillVideoSurfaceArea(FRAME_BUFFER, OVERHEAD_FRAME_X + 4, OVERHEAD_FRAME_Y + 4, OVERHEAD_FRAME_X + OVERHEAD_FRAME_WIDTH - 4, OVERHEAD_PANEL_Y, 0);
	}
	ColorFillVideoSurfaceArea(FRAME_BUFFER, sStartPointX_S, sStartPointY_S, sEndXS,	sEndYS, 0);

	InvalidateScreen();
	gfOverheadMapDirty = FALSE;

	{ SGPVSurface::Lock l(FRAME_BUFFER);
		UINT16* const pDestBuf         = l.Buffer<UINT16>();
		UINT32  const uiDestPitchBYTES = l.Pitch();

		{ // Begin Render Loop
			INT16 sAnchorPosX_M = sStartPointX_M;
			INT16 sAnchorPosY_M = sStartPointY_M;
			INT16 sAnchorPosX_S = sStartPointX_S;
			INT16 sAnchorPosY_S = sStartPointY_S;
			bool  bXOddFlag     = false;
			do
			{
				INT16 sTempPosX_M = sAnchorPosX_M;
				INT16 sTempPosY_M = sAnchorPosY_M;
				INT16 sTempPosX_S = sAnchorPosX_S;
				INT16 sTempPosY_S = sAnchorPosY_S;
				if (bXOddFlag) sTempPosX_S += 4;
				do
				{
					UINT32 const usTileIndex = FASTMAPROWCOLTOPOS(sTempPosY_M, sTempPosX_M);
					if (usTileIndex < GRIDSIZE)
					{
						INT16 const sHeight = GetOffsetLandHeight(usTileIndex) / 5;
						for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pLandStart; n; n = n->pPrevNode)
						{
							SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
							INT16         const  sX    = sTempPosX_S;
							INT16         const  sY    = sTempPosY_S - sHeight + gsRenderHeight / 5;
							pTile.vo->CurrentShade(n->ubShadeLevel);
							Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, pTile.vo, sX, sY, pTile.usSubIndex);
						}
					}

					sTempPosX_S += 8;
					++sTempPosX_M;
					--sTempPosY_M;
				}
				while (sTempPosX_S < sEndXS);

				if (bXOddFlag)
				{
					++sAnchorPosY_M;
				}
				else
				{
					++sAnchorPosX_M;
				}

				bXOddFlag = !bXOddFlag;
				sAnchorPosY_S += 2;
			}
			while (sAnchorPosY_S < sEndYS);
		}

		{ // Begin Render Loop
			INT16 sAnchorPosX_M = sStartPointX_M;
			INT16 sAnchorPosY_M = sStartPointY_M;
			INT16 sAnchorPosX_S = sStartPointX_S;
			INT16 sAnchorPosY_S = sStartPointY_S;
			bool  bXOddFlag     = false;
			do
			{
				INT16 sTempPosX_M = sAnchorPosX_M;
				INT16 sTempPosY_M = sAnchorPosY_M;
				INT16 sTempPosX_S = sAnchorPosX_S;
				INT16 sTempPosY_S = sAnchorPosY_S;
				if (bXOddFlag) sTempPosX_S += 4;
				do
				{
					UINT32 const usTileIndex = FASTMAPROWCOLTOPOS(sTempPosY_M, sTempPosX_M);
					if (usTileIndex < GRIDSIZE)
					{
						INT16 const sHeight         = GetOffsetLandHeight(usTileIndex) / 5;
						INT16 const sModifiedHeight = GetModifiedOffsetLandHeight(usTileIndex) / 5;

						for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pObjectHead; n; n = n->pNext)
						{
							if (n->usIndex >= NUMBEROFTILES) continue;
							// Don't render itempools!
							if (n->uiFlags & LEVELNODE_ITEM) continue;

							SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
							INT16         const  sX    = sTempPosX_S;
							INT16                sY    = sTempPosY_S;

							if (gTileDatabase[n->usIndex].uiFlags & IGNORE_WORLD_HEIGHT)
							{
								sY -= sModifiedHeight;
							}
							else
							{
								sY -= sHeight;
							}

							sY += gsRenderHeight / 5;

							pTile.vo->CurrentShade(n->ubShadeLevel);
							Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, pTile.vo, sX, sY, pTile.usSubIndex);
						}

						for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pShadowHead; n; n = n->pNext)
						{
							if (n->usIndex >= NUMBEROFTILES) continue;

							SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
							INT16         const  sX    = sTempPosX_S;
							INT16                sY    = sTempPosY_S - sHeight;

							sY += gsRenderHeight / 5;

							pTile.vo->CurrentShade(n->ubShadeLevel);
							Blt8BPPDataTo16BPPBufferShadow(pDestBuf, uiDestPitchBYTES, pTile.vo, sX, sY, pTile.usSubIndex);
						}

						for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pStructHead; n; n = n->pNext)
						{
							if (n->usIndex >= NUMBEROFTILES) continue;
							// Don't render itempools!
							if (n->uiFlags & LEVELNODE_ITEM) continue;

							SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
							INT16         const  sX    = sTempPosX_S;
							INT16                sY    = sTempPosY_S;

							if (gTileDatabase[n->usIndex].uiFlags & IGNORE_WORLD_HEIGHT)
							{
								sY -= sModifiedHeight;
							}
							else
							{
								sY -= sHeight;
							}

							sY += gsRenderHeight / 5;

							pTile.vo->CurrentShade(n->ubShadeLevel);
							Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, pTile.vo, sX, sY, pTile.usSubIndex);
						}
					}

					sTempPosX_S += 8;
					++sTempPosX_M;
					--sTempPosY_M;
				}
				while (sTempPosX_S < sEndXS);

				if (bXOddFlag)
				{
					++sAnchorPosY_M;
				}
				else
				{
					++sAnchorPosX_M;
				}

				bXOddFlag = !bXOddFlag;
				sAnchorPosY_S += 2;
			}
			while (sAnchorPosY_S < sEndYS);
		}

		{ // ROOF RENDR LOOP
			// Begin Render Loop
			INT16 sAnchorPosX_M = sStartPointX_M;
			INT16 sAnchorPosY_M = sStartPointY_M;
			INT16 sAnchorPosX_S = sStartPointX_S;
			INT16 sAnchorPosY_S = sStartPointY_S;
			bool  bXOddFlag     = false;
			do
			{
				INT16 sTempPosX_M = sAnchorPosX_M;
				INT16 sTempPosY_M = sAnchorPosY_M;
				INT16 sTempPosX_S = sAnchorPosX_S;
				INT16 sTempPosY_S = sAnchorPosY_S;
				if (bXOddFlag) sTempPosX_S += 4;
				do
				{
					UINT32 const usTileIndex = FASTMAPROWCOLTOPOS(sTempPosY_M, sTempPosX_M);
					if (usTileIndex < GRIDSIZE)
					{
						INT16 const sHeight = GetOffsetLandHeight(usTileIndex) / 5;

						for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pRoofHead; n; n = n->pNext)
						{
							if (n->usIndex >= NUMBEROFTILES)   continue;
							if (n->uiFlags & LEVELNODE_HIDDEN) continue;

							SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
							INT16         const  sX    = sTempPosX_S;
							INT16                sY    = sTempPosY_S - sHeight;

							sY -= WALL_HEIGHT / 5;
							sY += gsRenderHeight / 5;

							pTile.vo->CurrentShade(n->ubShadeLevel);

							// RENDER!
							Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, pTile.vo, sX, sY, pTile.usSubIndex);
						}
					}

					sTempPosX_S += 8;
					++sTempPosX_M;
					--sTempPosY_M;
				}
				while (sTempPosX_S < sEndXS);

				if (bXOddFlag)
				{
					++sAnchorPosY_M;
				}
				else
				{
					++sAnchorPosX_M;
				}

				bXOddFlag = !bXOddFlag;
				sAnchorPosY_S += 2;
			}
			while (sAnchorPosY_S < sEndYS);
		}
	}

	// OK, blacken out edges of smaller maps...
	if (gMapInformation.ubRestrictedScrollID != 0)
	{
		UINT16 const black = Get16BPPColor(FROMRGB(0, 0, 0));
		INT16 sX1;
		INT16 sX2;
		INT16 sY1;
		INT16 sY2;

		CalculateRestrictedMapCoords(NORTH, &sX1, &sY1, &sX2, &sY2, sEndXS, sEndYS);
		ColorFillVideoSurfaceArea(FRAME_BUFFER, sX1, sY1, sX2, sY2, black);

		CalculateRestrictedMapCoords(WEST, &sX1, &sY1, &sX2, &sY2, sEndXS, sEndYS);
		ColorFillVideoSurfaceArea(FRAME_BUFFER, sX1, sY1, sX2, sY2, black);

		CalculateRestrictedMapCoords(SOUTH, &sX1, &sY1, &sX2, &sY2, sEndXS, sEndYS);
		ColorFillVideoSurfaceArea(FRAME_BUFFER, sX1, sY1, sX2, sY2, black);

		CalculateRestrictedMapCoords(EAST, &sX1, &sY1, &sX2, &sY2, sEndXS, sEndYS);
		ColorFillVideoSurfaceArea(FRAME_BUFFER, sX1, sY1, sX2, sY2, black);

	}

	if (!fFromMapUtility)
	{ // Render border!
		RenderOverheadMapFrame();
	}

	// Update the save buffer
	BltVideoSurface(guiSAVEBUFFER, FRAME_BUFFER, 0, 0, NULL);
}


static void RenderOverheadOverlays(void)
{
	SGPVSurface::Lock l(FRAME_BUFFER);
	UINT16* const pDestBuf         = l.Buffer<UINT16>();
	UINT32  const uiDestPitchBYTES = l.Pitch();

	// Soldier overlay
	SGPVObject*        const marker = GetVObject(uiPERSONS);
	SOLDIERTYPE const* const sel    = gfTacticalPlacementGUIActive || !gfRadarCurrentGuyFlash ? 0 : GetSelectedMan();
	UINT16             const end    = gfTacticalPlacementGUIActive ? gTacticalStatus.Team[OUR_TEAM].bLastID : MAX_NUM_SOLDIERS;
	for (UINT32 i = 0; i < end; ++i)
	{
		SOLDIERTYPE const& s = GetMan(i);
		if (!s.bActive || !s.bInSector) continue;

		if (!gfTacticalPlacementGUIActive && s.bLastRenderVisibleValue == -1 && !(gTacticalStatus.uiFlags & SHOW_ALL_MERCS))
		{
			continue;
		}

		if (s.sGridNo == NOWHERE) continue;

		//Soldier is here.  Calculate his screen position based on his current gridno.
		INT16 sX;
		INT16 sY;
		GetOverheadScreenXYFromGridNo(s.sGridNo, &sX, &sY);
		//Now, draw his "doll"

		sX += STD_SCREEN_X;
		sY += STD_SCREEN_Y;

		//adjust for position.
		sX += 2;
		sY -= 5;

		sY -= s.sHeightAdjustment / 5; // Adjust for height

		UINT32 const shade =
			&s == sel           ? 2 :
			s.sHeightAdjustment ? 1 : // On roof
			0;
		marker->CurrentShade(shade);

		if (gfEditMode && GameMode::getInstance()->isEditorMode() && gpSelected && gpSelected->pSoldier == &s)
		{ //editor:  show the selected edited merc as the yellow one.
			Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, marker, sX, sY, 0);
		}
		else
		{
			UINT16 const region =
				!gfTacticalPlacementGUIActive                                ? s.bTeam :
				s.uiStatusFlags & SOLDIER_VEHICLE                            ? 9       :
				&s == gpTacticalPlacementSelectedSoldier                     ? 7       :
				&s == gpTacticalPlacementHilightedSoldier && s.uiStatusFlags ? 8       :
				s.bTeam;
			Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, marker, sX, sY, region);
			ETRLEObject const& e = marker->SubregionProperties(region);
			RegisterBackgroundRect(BGND_FLAG_SINGLE, sX + e.sOffsetX, sY + e.sOffsetY, e.usWidth, e.usHeight);
		}
	}

	// Items overlay
	if (!gfTacticalPlacementGUIActive)
	{
		CFOR_EACH_WORLD_ITEM(wi)
		{
			if (wi.bVisible != VISIBLE && !(gTacticalStatus.uiFlags & SHOW_ALL_ITEMS))
			{
				continue;
			}

			INT16 sX;
			INT16 sY;
			GetOverheadScreenXYFromGridNo(wi.sGridNo, &sX, &sY);

			sX += STD_SCREEN_X;
			sY += STD_SCREEN_Y;

			//adjust for position.
			sY += 6;

			UINT32 col;
			if (gsOveritemPoolGridNo == wi.sGridNo)
			{
				col = FROMRGB(255, 0, 0);
			}
			else if (gfRadarCurrentGuyFlash)
			{
				col = FROMRGB(0, 0, 0);
			}
			else switch (wi.bVisible)
			{
				case HIDDEN_ITEM:      col = FROMRGB(  0,   0, 255); break;
				case BURIED:           col = FROMRGB(255,   0,   0); break;
				case HIDDEN_IN_OBJECT: col = FROMRGB(  0,   0, 255); break;
				case INVISIBLE:        col = FROMRGB(  0, 255,   0); break;
				case VISIBLE:          col = FROMRGB(255, 255, 255); break;
				default:               abort();
			}
			PixelDraw(FALSE, sX, sY, Get16BPPColor(col), pDestBuf);
			InvalidateRegion(sX, sY, sX + 1, sY + 1);
		}
	}
}


static void ClickOverheadRegionCallbackPrimary(MOUSE_REGION* reg, UINT32 reason)
{
	if( gfTacticalPlacementGUIActive )
	{
		HandleTacticalPlacementClicksInOverheadMap(reason);
		return;
	}

	// Get new proposed center location.
	const GridNo pos = GetOverheadMouseGridNo();
	INT16 cell_x;
	INT16 cell_y;
	ConvertGridNoToCenterCellXY(pos, &cell_x, &cell_y);

	SetRenderCenter(cell_x, cell_y);

	KillOverheadMap();
}

static void ClickOverheadRegionCallbackSecondary(MOUSE_REGION* reg, UINT32 reason)
{
	if( gfTacticalPlacementGUIActive )
	{
		HandleTacticalPlacementClicksInOverheadMap(reason);
		return;
	}

	KillOverheadMap();
}


static GridNo InternalGetOverheadMouseGridNo(const INT dy)
{
	if (!(OverheadRegion.uiFlags & MSYS_MOUSE_IN_AREA)) return NOWHERE;

	// ATE: Adjust alogrithm values a tad to reflect map positioning
	INT16 mouse_x;
	INT16 mouse_y;
	GetOverheadMapMousePos(&mouse_x, &mouse_y);
	INT16 const sWorldScreenX = (mouse_x - STD_SCREEN_X - gsStartRestrictedX -  5) * 5;
	INT16       sWorldScreenY = (mouse_y - STD_SCREEN_Y - gsStartRestrictedY + dy) * 5;

	// Get new proposed center location.
	const GridNo grid_no = GetMapPosFromAbsoluteScreenXY(sWorldScreenX, sWorldScreenY);

	// Adjust for height.....
	sWorldScreenY += GetOffsetLandHeight(grid_no);
	sWorldScreenY -= gsRenderHeight;

	return GetMapPosFromAbsoluteScreenXY(sWorldScreenX, sWorldScreenY);
}


GridNo GetOverheadMouseGridNo(void)
{
	return InternalGetOverheadMouseGridNo(-8);
}


static GridNo GetOverheadMouseGridNoForFullSoldiersGridNo(void)
{
	return InternalGetOverheadMouseGridNo(0);
}


void CalculateRestrictedMapCoords( INT8 bDirection, INT16 *psX1, INT16 *psY1, INT16 *psX2, INT16 *psY2, INT16 sEndXS, INT16 sEndYS )
{
	switch( bDirection )
	{
		case NORTH:

			*psX1 = 0;
			*psX2 = sEndXS;
			*psY1 = 0;
			*psY2 = std::abs(NORMAL_MAP_SCREEN_TY - gsTopY) / 5;
			break;

		case WEST:

			*psX1 = 0;
			*psX2 = std::abs(-NORMAL_MAP_SCREEN_X - gsLeftX) / 5;
			*psY1 = 0;
			*psY2 = sEndYS;
			break;

		case SOUTH:

			*psX1 = 0;
			*psX2 = sEndXS;
			*psY1 = (NORMAL_MAP_SCREEN_HEIGHT - std::abs(NORMAL_MAP_SCREEN_BY - gsBottomY)) / 5;
			*psY2 = sEndYS;
			break;

		case EAST:

			*psX1 = (NORMAL_MAP_SCREEN_WIDTH - std::abs(NORMAL_MAP_SCREEN_X - gsRightX)) / 5;
			*psX2 = sEndXS;
			*psY1 = 0;
			*psY2 = sEndYS;
			break;

	}
}


static void CopyOverheadDBShadetablesFromTileset(void)
{
	// Loop through tileset
	for (size_t i = 0; i < NUMBEROFTILETYPES; ++i)
	{
		gSmTileSurf[i].vo->ShareShadetables(gTileSurfaceArray[i]->vo);
	}
}


void TrashOverheadMap(void)
{
	if (gubSmTileNum == TILESET_INVALID) return;
	gubSmTileNum = TILESET_INVALID;

	FOR_EACH(SMALL_TILE_SURF, i, gSmTileSurf)
	{
		DeleteVideoObject(i->vo);
	}
}
