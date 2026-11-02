#include "Button_System.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Logger.h"
#include "Cursor_Control.h"
#include "Cursors.h"
#include "Directories.h"
#include "English.h"
#include "Font.h"
#include "Font_Control.h"
#include "GameRes.h"
#include "GameSettings.h"
#include "GameLoop.h"
#include "GameVersion.h"
#include "HImage.h"
#include "Input.h"
#include "JA2_Splash.h"
#include "JAScreens.h"
#include "MainMenuScreen.h"
#include "MessageBoxScreen.h"
#include "Music_Control.h"
#include "ContentMusic.h"
#include "Object_Cache.h"
#include "Options_Screen.h"
#include "Render_Dirty.h"
#include "SGP.h"
#include "SaveLoadScreen.h"
#include "SysUtil.h"
#include "Text.h"
#include "Timer_Control.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "Video.h"
#include "WordWrap.h"
#include "UILayout.h"

#include <algorithm>
#include <string_theory/format>



// MENU ITEMS
enum
{
	NEW_GAME,
	LOAD_GAME,
	PREFERENCES,
	CREDITS,
	QUIT,
	NUM_MENU_ITEMS
};

#define MAINMENU_Y       277
#define MAINMENU_Y_SPACE  37


static BUTTON_PICS* iMenuImages[NUM_MENU_ITEMS];
static GUIButtonRef iMenuButtons[NUM_MENU_ITEMS];

namespace {
cache_key_t const guiMainMenuBackGroundImage{ LOADSCREENSDIR "/mainmenubackground.sti" };
cache_key_t const guiJa2LogoImage{ LOADSCREENSDIR "/ja2logo.sti" };

char const WIDE_BACKGROUND_FILE[] = "sti/loadscreens/mainmenubackground-wide.sti";
SGPVSurface* guiWideBackground = nullptr;
bool gfWideBackgroundTried = false;
}

static INT8    gbHandledMainMenu = 0;
static BOOLEAN fInitialRender    = FALSE;

static BOOLEAN gfMainMenuScreenEntry = FALSE;
static BOOLEAN gfMainMenuScreenExit = FALSE;

static ScreenID guiMainMenuExitScreen = MAINMENU_SCREEN;


extern BOOLEAN gfLoadGameUponEntry;


static void ExitMainMenu(void);
static void HandleMainMenuInput(void);
static void HandleMainMenuScreen(void);
static void RenderMainMenu(void);
static void RenderGameVersion(void);
static void RenderCopyright(void);


static void CaptureButtonAreaBackground() {
	SGPBox
		buttonArea = {
			std::numeric_limits<UINT16>().max(), std::numeric_limits<UINT16>().max(),
			0, 0
		};

	for( const auto &curButton : iMenuButtons ) {
		buttonArea.x = std::min( buttonArea.x, static_cast<UINT16>( curButton->Area.RegionTopLeftX ));
		buttonArea.y = std::min( buttonArea.y, static_cast<UINT16>( curButton->Area.RegionTopLeftY ));
		buttonArea.w = std::max( buttonArea.w, static_cast<UINT16>( curButton->Area.RegionBottomRightX ));
		buttonArea.h = std::max( buttonArea.h, static_cast<UINT16>( curButton->Area.RegionBottomRightY ));
	}

	buttonArea.w -= buttonArea.x;
	buttonArea.h -= buttonArea.y;

	BltVideoSurface( guiEXTRABUFFER, FRAME_BUFFER, buttonArea.x, buttonArea.y, &buttonArea );
}


static void RedrawActiveButtonBackgrounds() {
	for( const auto &curButton : iMenuButtons ) {
		if(( curButton->uiFlags ^ curButton->uiOldFlags ) & ( BUTTON_CLICKED_ON | BUTTON_ENABLED )) {
			const SGPBox
				buttonRect = {
					static_cast<UINT16>( curButton->X()), static_cast<UINT16>( curButton->Y()),
					static_cast<UINT16>( curButton->W()), static_cast<UINT16>( curButton->H())
				};

			BltVideoSurface( FRAME_BUFFER, guiEXTRABUFFER, buttonRect.x, buttonRect.y, &buttonRect );
		}
	}
}


ScreenID MainMenuScreenHandle(void)
{
	if (guiSplashStartTime + INTRO_SPLASH_DURATION > GetJA2Clock())
	{
		SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
		SetMusicMode(MUSIC_NONE);
		return MAINMENU_SCREEN; // The splash screen hasn't been up long enough yet.
	}
	if (guiSplashFrameFade)
	{
		// Fade the splash screen.
		if (guiSplashFrameFade > 2)
		{
			FRAME_BUFFER->ShadowRectUsingLowPercentTable(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		}
		else if (guiSplashFrameFade > 1)
		{
			FRAME_BUFFER->Fill(0);
		}
		else
		{
			SetMusicMode(MUSIC_MAIN_MENU);
			SetCurrentCursorFromDatabase(CURSOR_NORMAL);
		}

		guiSplashFrameFade--;

		InvalidateScreen();
		return MAINMENU_SCREEN;
	}

	if (gfMainMenuScreenEntry)
	{
		InitMainMenu();
		gfMainMenuScreenEntry = FALSE;
		gfMainMenuScreenExit  = FALSE;
		guiMainMenuExitScreen = MAINMENU_SCREEN;
		SetMusicMode(MUSIC_MAIN_MENU);
	}


	if (fInitialRender)
	{
		ClearMainMenu();
		RenderMainMenu();
		RenderGameVersion();
		RenderCopyright();
		CaptureButtonAreaBackground();

		fInitialRender = FALSE;
	}

	RedrawActiveButtonBackgrounds();

	RenderButtons();

	HandleMainMenuInput();
	HandleMainMenuScreen();

	if (gfMainMenuScreenExit)
	{
		ExitMainMenu();
		gfMainMenuScreenExit  = FALSE;
		gfMainMenuScreenEntry = TRUE;
	}

	if (guiMainMenuExitScreen != MAINMENU_SCREEN) gfMainMenuScreenEntry = TRUE;

	return guiMainMenuExitScreen;
}


static void SetMainMenuExitScreen(ScreenID uiNewScreen);


static void HandleMainMenuScreen(void)
{
	if (gbHandledMainMenu == 0) return;

	// Exit according to handled value!
	switch (gbHandledMainMenu)
	{
		case QUIT:
			gfMainMenuScreenExit = TRUE;
			requestGameExit();
			break;

		case LOAD_GAME:
			// Select the game which is to be restored
			guiPreviousOptionScreen = guiCurrentScreen;
			gbHandledMainMenu       = 0;
			gfSaveGame              = FALSE;
			SetMainMenuExitScreen(SAVE_LOAD_SCREEN);
			break;

		case PREFERENCES:
			guiPreviousOptionScreen = guiCurrentScreen;
			gbHandledMainMenu       = 0;
			SetMainMenuExitScreen(OPTIONS_SCREEN);
			break;

		case CREDITS:
			gbHandledMainMenu = 0;
			SetMainMenuExitScreen(CREDIT_SCREEN);
			break;
	}
}


static void CreateDestroyMainMenuButtons(BOOLEAN fCreate);


void InitMainMenu(void)
{
	CreateDestroyMainMenuButtons(TRUE);

	// If there are no saved games, disable the button
	if (!AreThereAnySavedGameFiles()) DisableButton(iMenuButtons[LOAD_GAME]);

	gbHandledMainMenu = 0;
	fInitialRender    = TRUE;

	SetPendingNewScreen(MAINMENU_SCREEN);
	guiMainMenuExitScreen = MAINMENU_SCREEN;

	InitGameOptions();

	DequeueAllInputEvents();
}


static void ExitMainMenu(void)
{
	CreateDestroyMainMenuButtons(FALSE);
	RemoveVObject(guiMainMenuBackGroundImage);
	RemoveVObject(guiJa2LogoImage);
	if (guiWideBackground)
	{
		DeleteVideoSurface(guiWideBackground);
		guiWideBackground = nullptr;
	}
	gfWideBackgroundTried = false;
	gMsgBox.uiExitScreen = MAINMENU_SCREEN;
}


static void MenuButtonCallback(GUI_BUTTON *btn, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP)
	{
		INT8 const bID = btn->GetUserData();

		gbHandledMainMenu = bID;
		RenderMainMenu();

		switch (gbHandledMainMenu)
		{
			case NEW_GAME:  SetMainMenuExitScreen(GAME_INIT_OPTIONS_SCREEN); break;
			case LOAD_GAME: if (_KeyDown(ALT)) gfLoadGameUponEntry = TRUE;   break;
		}
	}
}


static void HandleMainMenuInput(void)
{
	InputAtom InputEvent;
	while (DequeueSpecificEvent(&InputEvent, KEYBOARD_EVENTS))
	{
		if (InputEvent.usEvent == KEY_UP)
		{
			switch (InputEvent.usParam)
			{
				case 'q': if (_KeyDown(CTRL)) gbHandledMainMenu = QUIT; break;					
				case SDLK_ESCAPE: gbHandledMainMenu = QUIT; break;

				case 'c':
					if (_KeyDown(ALT)) gfLoadGameUponEntry = TRUE;
					gbHandledMainMenu = LOAD_GAME;
					break;

				case 'o':
					gbHandledMainMenu = PREFERENCES;
					break;

				case 's':
					gbHandledMainMenu = CREDITS;
					break;
			}
		}
	}
}


void ClearMainMenu(void)
{
	FRAME_BUFFER->Fill(0);
	InvalidateScreen();
}


static void SetMainMenuExitScreen(ScreenID const uiNewScreen)
{
	guiMainMenuExitScreen = uiNewScreen;
	gfMainMenuScreenExit  = TRUE;
}


static void CreateDestroyMainMenuButtons(BOOLEAN fCreate)
{
	static BOOLEAN fButtonsCreated = FALSE;

	if (fCreate)
	{
		if (fButtonsCreated) return;

		// Reset the variable that allows the user to ALT click on the continue save btn to load the save instantly
		gfLoadGameUponEntry = FALSE;

		// Load button images
		const char* const filename = GetMLGFilename(MLG_TITLETEXT);

		INT32 Slot;
		Slot = 0;
		iMenuImages[NEW_GAME]    = LoadButtonImage(filename, Slot, Slot, Slot + 1, Slot + 2, -1);
		iMenuImages[LOAD_GAME]   = UseLoadedButtonImage(iMenuImages[NEW_GAME],  6,  3,  4,  5, -1);
		iMenuImages[PREFERENCES] = UseLoadedButtonImage(iMenuImages[NEW_GAME],  7,  7,  8,  9, -1);
		iMenuImages[CREDITS]     = UseLoadedButtonImage(iMenuImages[NEW_GAME], 13, 10, 11, 12, -1);
		iMenuImages[QUIT]        = UseLoadedButtonImage(iMenuImages[NEW_GAME], 14, 14, 15, 16, -1);

		for (UINT32 cnt = 0; cnt < NUM_MENU_ITEMS; ++cnt)
		{
			BUTTON_PICS* const img = iMenuImages[cnt];
			const UINT16       w   = GetDimensionsOfButtonPic(img)->w;
			const INT16        x   = (SCREEN_WIDTH - w) / 2;
			const INT16        y   = STD_SCREEN_Y + MAINMENU_Y + cnt * MAINMENU_Y_SPACE;
			GUIButtonRef const b = QuickCreateButton(img, x, y, MSYS_PRIORITY_HIGHEST, MenuButtonCallback);
			iMenuButtons[cnt] = b;
			b->SetUserData(cnt);
		}

		fButtonsCreated = TRUE;
	}
	else
	{
		if (!fButtonsCreated) return;

		// Delete images/buttons
		for (UINT32 cnt = 0; cnt < NUM_MENU_ITEMS; ++cnt)
		{
			RemoveButton(iMenuButtons[cnt]);
			UnloadButtonImage(iMenuImages[cnt]);
		}
		fButtonsCreated = FALSE;
	}
}


/* The 1024x768 Wildfire menu background extended to 16:9 with new artwork at
 * the sides, if available. Loaded on first use, freed on leaving the menu. */
static SGPVSurface* GetWideMenuBackground(ETRLEObject const& bg)
{
	if (gfWideBackgroundTried) return guiWideBackground;
	gfWideBackgroundTried = true;
	if (bg.usWidth != 1024 || bg.usHeight != 768 || !GCM->doesGameResExists(WIDE_BACKGROUND_FILE)) return nullptr;

	try
	{
		guiWideBackground = AddVideoSurfaceFromFile(WIDE_BACKGROUND_FILE);
	}
	catch (std::exception const& e)
	{
		SLOGW("Could not load {}: {}", WIDE_BACKGROUND_FILE, e.what());
		return nullptr;
	}
	if (guiWideBackground->BPP() != 16 || guiWideBackground->Height() != bg.usHeight || guiWideBackground->Width() < bg.usWidth)
	{
		SLOGW("Ignoring {}: expected a 16 bpp image {} pixels high and at least {} wide", WIDE_BACKGROUND_FILE, bg.usHeight, bg.usWidth);
		DeleteVideoSurface(guiWideBackground);
		guiWideBackground = nullptr;
	}
	return guiWideBackground;
}


static void RenderMainMenu(void)
{
	SGPVObject const* const background = GetVObject(guiMainMenuBackGroundImage);
	ETRLEObject const& bg = background->SubregionProperties(0);
	if (bg.usWidth <= 640 && bg.usHeight <= 480)
	{
		BltVideoObject(FRAME_BUFFER, background,      0, STD_SCREEN_X,       STD_SCREEN_Y     );
		BltVideoObject(FRAME_BUFFER, guiJa2LogoImage, 0, STD_SCREEN_X + 188, STD_SCREEN_Y + 15);
		return;
	}

	// High resolution artwork (Wildfire ships a 1024x768 background and a
	// logo as wide as it). Compose the menu at its native size, then fit it
	// to the screen instead of cropping it.
	SGPBox box = FitImageToScreen(bg.usWidth, bg.usHeight, std::max<UINT16>(640, std::min(SCREEN_WIDTH, bg.usWidth)), std::max<UINT16>(480, std::min(SCREEN_HEIGHT, bg.usHeight)));

	// Wildfire's background extended to 16:9 with new artwork at the sides:
	// show as much of it as fits next to the original artwork.
	SGPVSurface* const wide = GetWideMenuBackground(bg);
	UINT16 w = bg.usWidth;
	UINT16 crop_x = 0;
	if (wide)
	{
		UINT32 const fits = UINT32(SCREEN_WIDTH) * bg.usHeight / box.h;
		w      = UINT16(std::clamp<UINT32>(fits, bg.usWidth, wide->Width()));
		crop_x = (wide->Width() - w) / 2;
		UINT16 const box_w = UINT16(UINT32(w) * box.h / bg.usHeight);
		box.x = (SCREEN_WIDTH - box_w) / 2;
		box.w = box_w;
	}

	SGPVSurface composed(w, bg.usHeight, 16);
	composed.Fill(0);
	SGPRect clip;
	clip.set(0, 0, w, bg.usHeight);
	SGPRect const old_clip = SetClippingRect(clip);
	if (wide)
	{
		SGPBox const src{ crop_x, 0, w, bg.usHeight };
		BltVideoSurface(&composed, wide, 0, 0, &src);
	}
	else
	{
		BltVideoObject(&composed, background, 0, -bg.sOffsetX, -bg.sOffsetY);
	}
	SGPVObject const* const logo = GetVObject(guiJa2LogoImage);
	ETRLEObject const& lg = logo->SubregionProperties(0);
	BltVideoObject(&composed, logo, 0, (w - lg.usWidth) / 2 - lg.sOffsetX, -lg.sOffsetY);
	SetClippingRect(old_clip);

	BltVideoSurfaceSmoothScaled(FRAME_BUFFER, &composed, box);
	// cover what remains of the margins of a widescreen display
	FillSideMarginsWithMirroredImage(FRAME_BUFFER, box);
}

void RenderGameVersion() {
	SetFontAttributes(FONT10ARIAL, FONT_MCOLOR_WHITE);
	MPrint(g_ui.m_versionPosition.iX, g_ui.m_versionPosition.iY, ST::format("{}", g_version_label));
}

void RenderCopyright() {
	DrawTextToScreen(gzCopyrightText, 0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, FONT10ARIAL, FONT_MCOLOR_WHITE, FONT_MCOLOR_BLACK, CENTER_JUSTIFIED);
}
