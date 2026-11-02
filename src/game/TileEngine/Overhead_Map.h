#ifndef __OVERHEADMAP_H
#define __OVERHEADMAP_H

#include "JA2Types.h"
#include "Types.h"
#include "World_Tileset_Enums.h"
#include "UILayout.h"

/* The overhead map itself is always 640x320 (the small tiles are 1/5 scale)
 * and drawn at STD_SCREEN_X/Y. Its frame (map_bord.sti) is 640x360 in the
 * original game. Wildfire's frame is 1024x648 with the map window at
 * (4,4)-(1020,608), and the tactical placement panel goes below it at
 * y = 608. With Wildfire's frame, the frame is widened to the screen width
 * and the 640x320 map is shown scaled up to fill its window. */
#define OVERHEAD_FRAME_X       TAC_LAYOUT(STD_SCREEN_X, 0)
#define OVERHEAD_FRAME_Y       TAC_LAYOUT(STD_SCREEN_Y, (SCREEN_HEIGHT -  768) / 2)
#define OVERHEAD_FRAME_WIDTH   TAC_LAYOUT(640, SCREEN_WIDTH)
#define OVERHEAD_FRAME_HEIGHT  TAC_LAYOUT(360,  648)
#define OVERHEAD_PANEL_Y       (OVERHEAD_FRAME_Y + TAC_LAYOUT(320, 608))


void InitNewOverheadDB(TileSetID);
void RenderOverheadMap( INT16 sStartPointX_M, INT16 sStartPointY_M, INT16 sStartPointX_S, INT16 sStartPointY_S, INT16 sEndXS, INT16 sEndYS, BOOLEAN fFromMapUtility );


void HandleOverheadMap(void);
BOOLEAN InOverheadMap(void);
void GoIntoOverheadMap(void);
void KillOverheadMap(void);

void CalculateRestrictedMapCoords( INT8 bDirection, INT16 *psX1, INT16 *psY1, INT16 *psX2, INT16 *psY2, INT16 sEndXS, INT16 sEndYS );

void TrashOverheadMap(void);

GridNo GetOverheadMouseGridNo(void);

/* Mouse position in the coordinates the 640x320 overhead map is rendered in
 * (at STD_SCREEN_X/Y), which differ from the screen ones when it is shown
 * scaled up. */
void GetOverheadMapMousePos(INT16* x, INT16* y);

extern BOOLEAN gfOverheadMapDirty;

#endif
