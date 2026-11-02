#include "UILayout.h"

#include "Directories.h"
#include "HImage.h"
#include "Logger.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "GamePolicy.h"
#include "JAScreens.h"
#include "MapScreen.h"
#include "ScreenIDs.h"
#include "Soldier_Control.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_theory/string>

#define MIN_INTERFACE_WIDTH       640
#define MIN_INTERFACE_HEIGHT      480
#define WILDFIRE_INTERFACE_WIDTH  1024
#define WILDFIRE_INTERFACE_HEIGHT  768

/**
 * Default screen layout.
 * It might be changed later when the window size is known for sure. */
UILayout g_ui(MIN_INTERFACE_WIDTH, MIN_INTERFACE_HEIGHT);


/** Constructor. */
UILayout::UILayout(UINT16 screenWidth, UINT16 screenHeight)
	:m_mapScreenWidth(MIN_INTERFACE_WIDTH), m_mapScreenHeight(MIN_INTERFACE_HEIGHT),
	m_screenWidth(screenWidth), m_screenHeight(screenHeight)
{
}


void UILayout::setScreenSize(UINT16 width, UINT16 height)
{
	if (width < MIN_INTERFACE_WIDTH || height < MIN_INTERFACE_HEIGHT)
	{
		ST::string err = ST::format("Failed to set screen resolution {} x {}", width, height);
		throw std::runtime_error(err.to_std_string());
	}
	m_screenWidth = width;
	m_screenHeight = height;
}


/** Check if the screen is bigger than original 640x480. */
bool UILayout::isBigScreen() const
{
	return (m_screenWidth > 640) || (m_screenHeight > 480);
}


void UILayout::detectMapScreenLayout(float displayAspectRatio)
{
	// The original map border is 379 pixels wide, Wildfire's is 763.
	try
	{
		AutoSGPImage img(CreateImage(INTERFACEDIR "/mbs.sti", IMAGE_ALLIMAGEDATA));
		m_wildfireMapScreen = img->usNumberOfObjects > 0 && img->pETRLEObject[0].usWidth > MIN_INTERFACE_WIDTH - 261;
	}
	catch (const std::exception& ex)
	{
		SLOGW("Could not determine the map screen layout: {}", ex.what());
		m_wildfireMapScreen = false;
	}
	try
	{
		AutoSGPImage img(CreateImage(INTERFACEDIR "/bottom_bar.sti", IMAGE_ALLIMAGEDATA));
		m_wildfireTacticalPanels = img->usNumberOfObjects > 0 && img->pETRLEObject[0].usWidth >= WILDFIRE_INTERFACE_WIDTH;
	}
	catch (const std::exception& ex)
	{
		SLOGW("Could not determine the tactical panel layout: {}", ex.what());
		m_wildfireTacticalPanels = false;
	}
	if (m_wildfireTacticalPanels) SLOGI("Using the Wildfire 1024 pixel wide tactical panels");
	if (!m_wildfireMapScreen && !m_wildfireTacticalPanels) return;

	SLOGI("Using the Wildfire 1024x768 map screen layout");
	if (m_screenWidth < WILDFIRE_INTERFACE_WIDTH && m_screenHeight < WILDFIRE_INTERFACE_HEIGHT)
	{
		// The configured resolution (usually the 640x480 default) is too small
		// in both directions, so it gets replaced. Use a height of 768 and
		// follow the display's aspect ratio for the width, so that the game
		// fills a widescreen display instead of being pillarboxed.
		UINT16 width = WILDFIRE_INTERFACE_WIDTH;
		if (displayAspectRatio > 0.0f)
		{
			// round to an even number of pixels
			int const w = int(std::lround(WILDFIRE_INTERFACE_HEIGHT * displayAspectRatio / 2)) * 2;
			width = UINT16(std::clamp(w, WILDFIRE_INTERFACE_WIDTH, 4 * WILDFIRE_INTERFACE_WIDTH));
		}
		SLOGI("Using a resolution of {}x{} for the Wildfire layout", width, WILDFIRE_INTERFACE_HEIGHT);
		setScreenSize(width, WILDFIRE_INTERFACE_HEIGHT);
	}
	else if (m_screenWidth < WILDFIRE_INTERFACE_WIDTH || m_screenHeight < WILDFIRE_INTERFACE_HEIGHT)
	{
		SLOGI("Increasing the resolution to at least {}x{} for the Wildfire map screen", WILDFIRE_INTERFACE_WIDTH, WILDFIRE_INTERFACE_HEIGHT);
		setScreenSize(std::max<UINT16>(m_screenWidth, WILDFIRE_INTERFACE_WIDTH), std::max<UINT16>(m_screenHeight, WILDFIRE_INTERFACE_HEIGHT));
	}
}


UINT16 UILayout::currentHeight() const             { return fInMapMode ? (m_wildfireMapScreen ? MAP_ORIGIN_Y + WILDFIRE_INTERFACE_HEIGHT : STD_SCREEN_Y + m_mapScreenHeight) : m_screenHeight; }
// Wildfire: clock and radar sit in the right part of its bottom panel (at 261, 647)
// Wildfire: clock and radar sit in the right part of its bottom panel (at 261, 647),
// in tactical they have a box of their own right of the buttons
UINT16 UILayout::get_CLOCK_X() const               { return fInMapMode ? (MAP_ORIGIN_X + MAP_LAYOUT(554, 261 + 668)) : m_teamPanelPosition.iX + m_teamPanelSlotsTotalWidth + TAC_LAYOUT(56, 108); }
UINT16 UILayout::get_CLOCK_Y() const               { return fInMapMode ? (m_wildfireMapScreen ? MAP_ORIGIN_Y + 647 + 97 : currentHeight() - 23) : currentHeight() - TAC_LAYOUT(23, 26); }
UINT16 UILayout::get_RADAR_WINDOW_X() const        { return fInMapMode ? (MAP_ORIGIN_X + MAP_LAYOUT(543, 261 + 665)) : m_teamPanelPosition.iX + m_teamPanelSlotsTotalWidth + TAC_LAYOUT(45, 96); }
UINT16 UILayout::get_RADAR_WINDOW_TM_Y() const     { return fInMapMode ? (m_wildfireMapScreen ? MAP_ORIGIN_Y + 647 + 12 : currentHeight() - 107) : currentHeight() - TAC_LAYOUT(107, 110); }
UINT16 UILayout::get_INV_INTERFACE_START_Y() const { return m_screenHeight - INV_INTERFACE_HEIGHT;                                  }


void UILayout::recalculatePositions()
{
	m_teamPanelSlotsTotalWidth = getTeamPanelNumSlots() * TEAMPANEL_SLOT_WIDTH;
	UINT16 tpXOffset = (m_screenWidth - m_teamPanelSlotsTotalWidth - TEAMPANEL_BUTTONSBOX_WIDTH) / 2;
	UINT16 tpYOffset = m_screenHeight - TEAMPANEL_HEIGHT;
	m_teamPanelPosition.set(tpXOffset, tpYOffset);
	m_teamPanelWidth = m_teamPanelSlotsTotalWidth + TEAMPANEL_BUTTONSBOX_WIDTH;

	UINT16 startInvY = get_INV_INTERFACE_START_Y();
	UINT16 startX    = INTERFACE_START_X;

	m_stdScreenOffsetX            = (m_screenWidth - MIN_INTERFACE_WIDTH) / 2;
	m_stdScreenOffsetY            = (m_screenHeight - MIN_INTERFACE_HEIGHT) / 2;
	if (m_wildfireMapScreen)
	{
		m_mapScreenOffsetX = (m_screenWidth  - WILDFIRE_INTERFACE_WIDTH)  / 2;
		m_mapScreenOffsetY = (m_screenHeight - WILDFIRE_INTERFACE_HEIGHT) / 2;
	}
	else
	{
		m_mapScreenOffsetX = m_stdScreenOffsetX;
		m_mapScreenOffsetY = m_stdScreenOffsetY;
	}

	// tactical screen inventory position
	m_invSlotPositionTac[HELMETPOS           ].set(startX + 344, startInvY +   6);
	m_invSlotPositionTac[VESTPOS             ].set(startX + 344, startInvY +  35);
	m_invSlotPositionTac[LEGPOS              ].set(startX + 344, startInvY +  95);
	m_invSlotPositionTac[HEAD1POS            ].set(startX + 226, startInvY +   6);
	m_invSlotPositionTac[HEAD2POS            ].set(startX + 226, startInvY +  30);
	m_invSlotPositionTac[HANDPOS             ].set(startX + 226, startInvY +  84);
	m_invSlotPositionTac[SECONDHANDPOS       ].set(startX + 226, startInvY + 108);
	m_invSlotPositionTac[BIGPOCK1POS         ].set(startX + 468, startInvY +   5);
	m_invSlotPositionTac[BIGPOCK2POS         ].set(startX + 468, startInvY +  29);
	m_invSlotPositionTac[BIGPOCK3POS         ].set(startX + 468, startInvY +  53);
	m_invSlotPositionTac[BIGPOCK4POS         ].set(startX + 468, startInvY +  77);
	m_invSlotPositionTac[SMALLPOCK1POS       ].set(startX + 396, startInvY +   5);
	m_invSlotPositionTac[SMALLPOCK2POS       ].set(startX + 396, startInvY +  29);
	m_invSlotPositionTac[SMALLPOCK3POS       ].set(startX + 396, startInvY +  53);
	m_invSlotPositionTac[SMALLPOCK4POS       ].set(startX + 396, startInvY +  77);
	m_invSlotPositionTac[SMALLPOCK5POS       ].set(startX + 432, startInvY +   5);
	m_invSlotPositionTac[SMALLPOCK6POS       ].set(startX + 432, startInvY +  29);
	m_invSlotPositionTac[SMALLPOCK7POS       ].set(startX + 432, startInvY +  53);
	m_invSlotPositionTac[SMALLPOCK8POS       ].set(startX + 432, startInvY +  77);

	// map screen inventory position
	m_invSlotPositionMap[HELMETPOS           ].set(m_mapScreenOffsetX + 204, m_mapScreenOffsetY + 116);
	m_invSlotPositionMap[VESTPOS             ].set(m_mapScreenOffsetX + 204, m_mapScreenOffsetY + 145);
	m_invSlotPositionMap[LEGPOS              ].set(m_mapScreenOffsetX + 204, m_mapScreenOffsetY + 205);
	m_invSlotPositionMap[HEAD1POS            ].set(m_mapScreenOffsetX +  21, m_mapScreenOffsetY + 116);
	m_invSlotPositionMap[HEAD2POS            ].set(m_mapScreenOffsetX +  21, m_mapScreenOffsetY + 140);
	m_invSlotPositionMap[HANDPOS             ].set(m_mapScreenOffsetX +  21, m_mapScreenOffsetY + 194);
	m_invSlotPositionMap[SECONDHANDPOS       ].set(m_mapScreenOffsetX +  21, m_mapScreenOffsetY + 218);
	m_invSlotPositionMap[BIGPOCK1POS         ].set(m_mapScreenOffsetX +  98, m_mapScreenOffsetY + 251);
	m_invSlotPositionMap[BIGPOCK2POS         ].set(m_mapScreenOffsetX +  98, m_mapScreenOffsetY + 275);
	m_invSlotPositionMap[BIGPOCK3POS         ].set(m_mapScreenOffsetX +  98, m_mapScreenOffsetY + 299);
	m_invSlotPositionMap[BIGPOCK4POS         ].set(m_mapScreenOffsetX +  98, m_mapScreenOffsetY + 323);
	m_invSlotPositionMap[SMALLPOCK1POS       ].set(m_mapScreenOffsetX +  22, m_mapScreenOffsetY + 251);
	m_invSlotPositionMap[SMALLPOCK2POS       ].set(m_mapScreenOffsetX +  22, m_mapScreenOffsetY + 275);
	m_invSlotPositionMap[SMALLPOCK3POS       ].set(m_mapScreenOffsetX +  22, m_mapScreenOffsetY + 299);
	m_invSlotPositionMap[SMALLPOCK4POS       ].set(m_mapScreenOffsetX +  22, m_mapScreenOffsetY + 323);
	m_invSlotPositionMap[SMALLPOCK5POS       ].set(m_mapScreenOffsetX +  60, m_mapScreenOffsetY + 251);
	m_invSlotPositionMap[SMALLPOCK6POS       ].set(m_mapScreenOffsetX +  60, m_mapScreenOffsetY + 275);
	m_invSlotPositionMap[SMALLPOCK7POS       ].set(m_mapScreenOffsetX +  60, m_mapScreenOffsetY + 299);
	m_invSlotPositionMap[SMALLPOCK8POS       ].set(m_mapScreenOffsetX +  60, m_mapScreenOffsetY + 323);

	m_invCamoRegion.set(SM_BODYINV_X, SM_BODYINV_Y);

	if (m_wildfireTacticalPanels)
	{
		m_progress_bar_box.set((m_screenWidth - WILDFIRE_INTERFACE_WIDTH) / 2 + 5, 2, WILDFIRE_INTERFACE_WIDTH - 10, 12);
	}
	else
	{
		m_progress_bar_box.set(STD_SCREEN_X + 5, 2, MIN_INTERFACE_WIDTH - 10, 12);
	}
	m_moneyButtonLoc.set(startX + 343, startInvY + 11);
	m_MoneyButtonLocMap.set(m_mapScreenOffsetX + 174, m_mapScreenOffsetY + 115);

	m_VIEWPORT_START_X            = 0;
	m_VIEWPORT_START_Y            = 0;
	m_VIEWPORT_WINDOW_START_Y     = 0;
	m_VIEWPORT_END_X              = m_screenWidth;
	m_VIEWPORT_END_Y              = m_screenHeight - 120;
	m_VIEWPORT_WINDOW_END_Y       = m_screenHeight - 120;
	m_tacticalMapCenterX          = (m_VIEWPORT_END_X - m_VIEWPORT_START_X) / 2;
	m_tacticalMapCenterY          = (m_VIEWPORT_END_Y - m_VIEWPORT_START_Y) / 2;

	m_worldClippingRect.set(0, 0, m_screenWidth, m_screenHeight - 120);

	m_contractPosition.set(       m_mapScreenOffsetX + 120, m_mapScreenOffsetY +  50);
	m_attributePosition.set(      m_mapScreenOffsetX + 220, m_mapScreenOffsetY + 150);
	m_trainPosition.set(          m_mapScreenOffsetX + 160, m_mapScreenOffsetY + 150);
	m_vehiclePosition.set(        m_mapScreenOffsetX + 160, m_mapScreenOffsetY + 150);
	m_repairPosition.set(         m_mapScreenOffsetX + 160, m_mapScreenOffsetY + 150);
	m_assignmentPosition.set(     m_mapScreenOffsetX + 120, m_mapScreenOffsetY + 150);
	m_squadPosition.set(          m_mapScreenOffsetX + 160, m_mapScreenOffsetY + 150);
	m_versionPosition.set(        10, m_screenHeight - 15);
}

/** Get X position of tactical textbox. */
UINT16 UILayout::getTacticalTextBoxX() const
{

	if ( guiCurrentScreen == MAP_SCREEN )
	{
		return MAP_ORIGIN_X + 110;
	}
	else
	{
		return 110;
	}
}

/** Get Y position of tactical textbox. */
UINT16 UILayout::getTacticalTextBoxY() const
{
	if ( guiCurrentScreen == MAP_SCREEN )
	{
		return DEFAULT_EXTERN_PANEL_Y_POS;
	}
	else
	{
		return 20;
	}
}

UINT16 UILayout::getTeamPanelNumSlots() const
{
	if (!GCM || !GCM->getGamePolicy())
	{
		throw std::runtime_error("ContentManager is not initialized yet. Unable to determine num of team slots");
	}

	// Wildfire's team panel graphic has room for exactly ten mercs
	if (m_wildfireTacticalPanels) return 10;

	int numSlots = std::min({(int)gamepolicy(squad_size), (m_screenWidth - TEAMPANEL_BUTTONSBOX_WIDTH) / TEAMPANEL_SLOT_WIDTH, 12});
	numSlots = std::max((int)numSlots, 6);
	return numSlots;
}
