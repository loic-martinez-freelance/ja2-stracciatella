#ifndef _UI_LAYOUT_H_
#define _UI_LAYOUT_H_

#include "Types.h"

/////////////////////////////////////////////////////////////
// defines
/////////////////////////////////////////////////////////////

#define NUM_INVENTORY_SLOTS     (19)

/* Following defines allow us to not change the old code too much.
 * It will help to preserve original Stracciatella codebase. */

#define SCREEN_HEIGHT                   (g_ui.m_screenHeight)
#define SCREEN_WIDTH                    (g_ui.m_screenWidth)
#define INV_INTERFACE_START_Y           (g_ui.get_INV_INTERFACE_START_Y())
#define INV_INTERFACE_HEIGHT            (140)                                 // height of the bottom bar single-merc inventory panel
#define INTERFACE_START_X               (g_ui.m_teamPanelPosition.iX)
#define INTERFACE_START_Y               (g_ui.m_teamPanelPosition.iY)
#define gsVIEWPORT_START_X              (g_ui.m_VIEWPORT_START_X)
#define gsVIEWPORT_START_Y              (g_ui.m_VIEWPORT_START_Y)
#define gsVIEWPORT_WINDOW_START_Y       (g_ui.m_VIEWPORT_WINDOW_START_Y)
#define gsVIEWPORT_END_X                (g_ui.m_VIEWPORT_END_X)
#define gsVIEWPORT_END_Y                (g_ui.m_VIEWPORT_END_Y)
#define gsVIEWPORT_WINDOW_END_Y         (g_ui.m_VIEWPORT_WINDOW_END_Y)
#define STD_SCREEN_X                    (g_ui.m_stdScreenOffsetX)
#define STD_SCREEN_Y                    (g_ui.m_stdScreenOffsetY)
#define MAP_SCREEN_WIDTH                (g_ui.m_mapScreenWidth)
#define MAP_SCREEN_HEIGHT               (g_ui.m_mapScreenHeight)
/* Origin of the strategic map screen. Equals STD_SCREEN_X/Y for the original
 * 640x480 layout; the Wildfire layout is 1024x768. */
#define MAP_ORIGIN_X                    (g_ui.m_mapScreenOffsetX)
#define MAP_ORIGIN_Y                    (g_ui.m_mapScreenOffsetY)
/* Pick a value depending on the tactical bottom panels (original or Wildfire). */
#define TAC_LAYOUT(original, wildfire)  (g_ui.m_wildfireTacticalPanels ? (wildfire) : (original))
/* Pick a value depending on the map screen layout (original or Wildfire). */
#define MAP_LAYOUT(original, wildfire)  (g_ui.m_wildfireMapScreen ? (wildfire) : (original))

/* x of the item description box in the tactical inventory panel: over the
 * inventory in the original layout; in Wildfire's, filling the inside of the
 * empty box right of it (x 540 to 911 of the 1024 pixel panel, plus the width
 * the panel is widened by), so the 320 pixel box is stretched by the extra
 * width. */
#define ITEMDESC_PANEL_X                (INTERFACE_START_X + TAC_LAYOUT(214, 540))
#define ITEMDESC_PANEL_EXTRA_WIDTH      TAC_LAYOUT(0, 911 - 540 - 320 + g_ui.m_teamPanelExtraWidth)

#define SM_BODYINV_X                    (INTERFACE_START_X + 244)
#define SM_BODYINV_Y                    (INV_INTERFACE_START_Y + 6)
#define SM_INVINTERFACE_WIDTH           (532)    // width of the single-merc inventory panel excluding the right-side buttons and minimap

#define EDITOR_TASKBAR_HEIGHT           (120)
#define EDITOR_TASKBAR_POS_Y            (UINT16)(SCREEN_HEIGHT - EDITOR_TASKBAR_HEIGHT)

#define DEFAULT_EXTERN_PANEL_X_POS      (STD_SCREEN_X + 320)
#define DEFAULT_EXTERN_PANEL_Y_POS      (STD_SCREEN_Y + 40)

#define TEAMPANEL_SLOT_WIDTH            (83)     // width of one slot in the bottom team panel
#define TEAMPANEL_BUTTONSBOX_WIDTH      TAC_LAYOUT(142, 194) // width of the container of the buttons (and, for Wildfire, the radar) on the right of team panel
#define TEAMPANEL_HEIGHT                (120)    // height of the bottom bar team panel


/////////////////////////////////////////////////////////////
// type definitions
/////////////////////////////////////////////////////////////

// USED TO SETUP REGION POSITIONS, ETC
struct INV_REGION_DESC
{
	UINT16     uX;
	UINT16     uY;

	void set(UINT16 x, UINT16 y)
	{
		uX = x;
		uY = y;
	}
};


struct MoneyLoc
{
	UINT16 x;
	UINT16 y;

	void set(UINT16 _x, UINT16 _y)
	{
		x = _x;
		y = _y;
	}
};


/** User Interface layout definition. */
struct UILayout
{
public:
	UINT16                m_mapScreenWidth;
	UINT16                m_mapScreenHeight;
	UINT16                m_screenWidth;
	UINT16                m_screenHeight;
	INV_REGION_DESC       m_invSlotPositionMap[NUM_INVENTORY_SLOTS];      /**< Map screen inventory slots positions  */
	INV_REGION_DESC       m_invSlotPositionTac[NUM_INVENTORY_SLOTS];      /**< Tactical screen Inventory slots positions */
	INV_REGION_DESC       m_invCamoRegion;                                /**< Camo (body) region in the inventory. */

	SGPBox                m_progress_bar_box;
	MoneyLoc              m_moneyButtonLoc;
	MoneyLoc              m_MoneyButtonLocMap;

	/** Viewport coordiantes.
	 * Viewport is the area of the screen where tactical map is displayed.
	 * For 640x480 it is (320, 180) */
	UINT16                m_VIEWPORT_START_X;
	UINT16                m_VIEWPORT_START_Y;
	UINT16                m_VIEWPORT_WINDOW_START_Y;

	UINT16                m_VIEWPORT_END_X;
	UINT16                m_VIEWPORT_END_Y;
	UINT16                m_tacticalMapCenterX;                           /**< Center of the tactical map (for 640x480 it is (320, 180)). */
	UINT16                m_tacticalMapCenterY;                           /**< Center of the tactical map (for 640x480 it is (320, 180)). */

	UINT16                m_VIEWPORT_WINDOW_END_Y;

	SGPRect               m_worldClippingRect;

	// Map screen interface
	SGPPoint              m_versionPosition;
	SGPPoint              m_contractPosition;
	SGPPoint              m_attributePosition;
	SGPPoint              m_trainPosition;
	SGPPoint              m_vehiclePosition;
	SGPPoint              m_repairPosition;
	SGPPoint              m_assignmentPosition ;
	SGPPoint              m_squadPosition ;

	// Tactical screen bottom bar
	// It can be in the "team" (TEAM) or the "single merc inventory" (SM or INV_) mode. Both modes have the same
	// width, but the single-merc mode is slightly taller.
	SGPPoint              m_teamPanelPosition;              // offset position of the bottom bar
	UINT16                m_teamPanelSlotsTotalWidth;       // total width of all team slots in the bottom team panel
	UINT16                m_teamPanelWidth;                 // width of the entire team panel including slots and buttons
	UINT16                m_teamPanelExtraWidth = 0;        // width added to the Wildfire panels to fill the screen
	UINT16                m_teamPanelButtonsBoxX;           // x of the buttons box (and radar) relative to the panel

	/** Jagged Alliance 2: Wildfire 6 ships a 1024x768 strategic map screen
	 * (bigger map, message log in the left column, new bottom panel). */
	bool                  m_wildfireMapScreen = false;
	/** Wildfire 6 also ships 1024 pixel wide tactical bottom panels with ten
	 * merc slots and the radar in a box of its own. */
	bool                  m_wildfireTacticalPanels = false;
	UINT16                m_mapScreenOffsetX = 0;         /** Offset of the strategic map screen */
	UINT16                m_mapScreenOffsetY = 0;         /** Offset of the strategic map screen */

	UINT16                m_stdScreenOffsetX;             /** Offset of the standard (640x480) window */
	UINT16                m_stdScreenOffsetY;             /** Offset of the standard (640x480) window */

	/** Constructor.
	 * @param screenWidth Screen width
	 * @param screenHeight Screen height */
	UILayout(UINT16 screenWidth, UINT16 screenHeight);

	/** Set new screen size. Element positions should be recalculated after setting this. @see UILayout::recalculatePositions */
	void setScreenSize(UINT16 width, UINT16 height);

	/** Check if the screen is bigger than original 640x480. */
	bool isBigScreen() const;

	/** Detect the Wildfire 1024x768 map screen from the game data. Enlarges
	 * the screen to at least 1024x768 if needed. Must be called after the
	 * game data has been loaded and before recalculatePositions().
	 * When the configured resolution is smaller than 1024x768 in both
	 * directions, it is replaced by one 768 pixels high whose width follows
	 * displayAspectRatio (width / height of the display, 0 if unknown). */
	void detectMapScreenLayout(float displayAspectRatio);

	UINT16 currentHeight() const;
	UINT16 get_CLOCK_X() const;
	UINT16 get_CLOCK_Y() const;
	UINT16 get_INV_INTERFACE_START_Y() const;
	UINT16 get_RADAR_WINDOW_X() const;
	UINT16 get_RADAR_WINDOW_TM_Y() const;

	/** Get X position of tactical textbox. */
	UINT16 getTacticalTextBoxX() const;

	/** Get Y position of tactical textbox. */
	UINT16 getTacticalTextBoxY() const;

	/** Number of displayable slots in the team panel, based on the game policy and screen width. */
	UINT16 getTeamPanelNumSlots() const;

	/** Recalculate UI elements' positions after changing screen size.
	 *  This method requires the game data to be loaded, but it should be called before most other the application initialization is done.
	 */
	void recalculatePositions();
};

/////////////////////////////////////////////////////////////
// external declarations
/////////////////////////////////////////////////////////////

extern UILayout g_ui;

/////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////

#endif
