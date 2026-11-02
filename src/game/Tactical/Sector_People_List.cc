#include "Sector_People_List.h"

#include "Arms_Dealer.h"
#include "Arms_Dealer_Init.h"
#include "Directories.h"
#include "Font.h"
#include "Font_Control.h"
#include "Handle_UI.h"
#include "Input.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "Message.h"
#include "MouseSystem.h"
#include "Overhead.h"
#include "PopUpBox.h"
#include "RenderWorld.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "UILayout.h"
#include "VObject.h"
#include "VSurface.h"

#include <string_theory/format>
#include <string_theory/string>

#include <algorithm>
#include <cmath>
#include <vector>


namespace
{
struct PersonEntry
{
	SoldierID  id;
	GridNo     gridno;
	INT8       level;
	INT16      distance;
	ST::string name;
	ST::string position;
	bool       known;
};
}

// The first box line is the title, the people follow
static constexpr INT32 FIRST_ENTRY_LINE = 1;
// Leave room for the title line
static constexpr size_t MAX_ENTRIES = MAX_POPUP_BOX_STRING_COUNT - FIRST_ENTRY_LINE;

static PopUpBox*                g_box        = NO_POPUP_BOX;
static SGPVObject*              g_border     = nullptr;
static SGPVSurface*             g_background = nullptr;
static MOUSE_REGION             g_mask_region;
static std::vector<PersonEntry> g_entries;


bool IsSectorPeopleListShown()
{
	return g_box != NO_POPUP_BOX;
}


static bool IsImportantPerson(SOLDIERTYPE const& s)
{
	if (!s.bInSector)                      return false;
	if (s.ubProfile == NO_PROFILE)         return false;
	if (s.ubProfile >= NUM_PROFILES)       return false;
	if (s.bTeam == OUR_TEAM)               return false;
	// Enemies are covered by Ctrl+E
	if (s.bTeam == ENEMY_TEAM)             return false;
	if (s.uiStatusFlags & SOLDIER_DEAD)    return false;
	if (s.uiStatusFlags & SOLDIER_VEHICLE) return false;
	if (s.bLife <= 0)                      return false;
	return true;
}


static ST::string DealerTag(ProfileID const pid)
{
	if (!IsMercADealer(pid)) return {};
	switch (GetTypeOfArmsDealer(GetArmsDealerIDFromMercID(pid)))
	{
		case ARMS_DEALER_REPAIRS: return "repairs";
		case NOT_VALID_DEALER:    return {};
		default:                  return "shop";
	}
}


// Screen relative direction, as the isometric view shows it
static char const* ScreenDirection(GridNo const from, GridNo const to)
{
	INT16 fx, fy, tx, ty;
	ConvertGridNoToXY(from, &fx, &fy);
	ConvertGridNoToXY(to,   &tx, &ty);
	INT32 const dx = tx - fx;
	INT32 const dy = ty - fy;
	// A tile step east goes down-right, a step north goes up-right
	double const sx = (dx - dy) * 2.0;
	double const sy = -(dx + dy) * 1.0;
	if (sx == 0 && sy == 0) return "";

	static char const* const names[] =
	{
		"right", "up-right", "up", "up-left", "left", "down-left", "down", "down-right"
	};
	double const angle  = std::atan2(sy, sx); // -pi..pi, counter-clockwise from right
	int    const octant = (int)std::lround(angle / (3.14159265358979323846 / 4)) & 7;
	return names[octant];
}


static ST::string DescribePosition(SOLDIERTYPE const& s, SOLDIERTYPE const* const ref, INT16* const distance)
{
	ST::string const roof = s.bLevel ? ", on a roof" : "";
	if (!ref)
	{
		*distance = 0;
		return s.bLevel ? "on a roof" : "";
	}

	INT16 const d = PythSpacesAway(ref->sGridNo, s.sGridNo);
	*distance = d;
	if (d == 0) return ST::format("next to {}{}", ref->name, roof);
	return ST::format("{} {} {}{}", d, d == 1 ? "tile" : "tiles",
		ScreenDirection(ref->sGridNo, s.sGridNo), roof);
}


// The merc distances are measured from: the selected one, or else any merc in
// the sector
static SOLDIERTYPE const* GetReferenceMerc()
{
	SOLDIERTYPE const* const sel = GetSelectedMan();
	if (sel && sel->bInSector) return sel;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (s->bInSector && s->bLife > 0) return s;
	}
	return nullptr;
}


static void GatherEntries()
{
	g_entries.clear();
	SOLDIERTYPE const* const ref = GetReferenceMerc();

	FOR_EACH_SOLDIER(i)
	{
		SOLDIERTYPE const& s = *i;
		if (!IsImportantPerson(s)) continue;

		MERCPROFILESTRUCT const& p = GetProfile(s.ubProfile);
		ST::string const tag = DealerTag(s.ubProfile);
		bool const known =
			!tag.empty() ||
			(p.ubMiscFlags2 & PROFILE_MISC_FLAG2_SEEN_BY_PLAYER) ||
			p.ubLastDateSpokenTo != 0 ||
			s.bVisible == TRUE;

		PersonEntry e;
		e.id     = s.ubID;
		e.gridno = s.sGridNo;
		e.level  = s.bLevel;
		e.known  = known;
		if (known)
		{
			ST::string const& name = p.zNickname.empty() ? p.zName : p.zNickname;
			e.name = tag.empty() ? name : ST::format("{} ({})", name, tag);
		}
		else
		{
			e.name = "Unknown";
		}
		e.position = DescribePosition(s, ref, &e.distance);
		g_entries.push_back(e);
	}

	std::stable_sort(g_entries.begin(), g_entries.end(),
		[](PersonEntry const& a, PersonEntry const& b) { return a.distance < b.distance; });
	if (g_entries.size() > MAX_ENTRIES) g_entries.resize(MAX_ENTRIES);
}


static INT32 GetLineUnderMouse()
{
	if (!g_box) return -1;
	SGPBox const& area = GetBoxArea(g_box);
	if (gusMouseXPos < area.x || gusMouseXPos >= area.x + area.w) return -1;

	INT32 const top = area.y + GetTopMarginSize(g_box);
	INT32 const dy  = GetLineSpace(g_box) + GetFontHeight(GetBoxFont(g_box));
	if (gusMouseYPos < top) return -1;
	INT32 const line = (gusMouseYPos - top) / dy;
	if (line < FIRST_ENTRY_LINE) return -1;
	if (line >= FIRST_ENTRY_LINE + (INT32)g_entries.size()) return -1;
	return line;
}


static void LocatePerson(PersonEntry const& e)
{
	// The person may have moved since the list was built
	SOLDIERTYPE const& s = GetMan(e.id);
	bool   const still_here = IsImportantPerson(s);
	GridNo const gridno     = still_here ? s.sGridNo : e.gridno;
	INT8   const level      = still_here ? s.bLevel  : e.level;

	InternalLocateGridNo(gridno, TRUE);
	BeginMultiPurposeLocator(gridno, level);
}


static void MaskMoveCallback(MOUSE_REGION*, UINT32 const reason)
{
	if (!g_box) return;
	if (reason & MSYS_CALLBACK_REASON_LOST_MOUSE)
	{
		UnHighLightBox(g_box);
		return;
	}
	INT32 const line = GetLineUnderMouse();
	UnHighLightBox(g_box);
	if (line != -1) HighLightBoxLine(g_box, line);
}


static void MaskButtonCallback(MOUSE_REGION*, UINT32 const reason)
{
	if (!g_box) return;
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP)
	{
		INT32 const line = GetLineUnderMouse();
		if (line != -1)
		{
			PersonEntry const e = g_entries[line - FIRST_ENTRY_LINE];
			RemoveSectorPeopleList();
			LocatePerson(e);
			return;
		}
		RemoveSectorPeopleList();
	}
	else if (reason & MSYS_CALLBACK_REASON_RBUTTON_UP)
	{
		RemoveSectorPeopleList();
	}
}


static BOOLEAN KeyboardHook(InputAtom* const e)
{
	if (!g_box) return FALSE;
	if (e->usEvent == KEY_UP &&
		(e->usParam == SDLK_ESCAPE || e->usParam == SDLK_RETURN))
	{
		RemoveSectorPeopleList();
	}
	else if (e->usEvent == KEY_DOWN && e->usParam == 'p' && (e->usKeyState & CTRL_DOWN))
	{
		RemoveSectorPeopleList();
	}
	// Swallow every key while the list is shown
	return TRUE;
}


static void CreateBox()
{
	g_border     = AddVideoObjectFromFile(INTERFACEDIR "/popup.sti");
	g_background = AddVideoSurfaceFromFile(INTERFACEDIR "/popupbackground.pcx");

	SGPPoint const pos = { 0, 0 };
	PopUpBox* const box = CreatePopUpBox(pos, 0, FRAME_BUFFER, g_border, g_background, 6, 6, 6, 6, 2);
	g_box = box;

	size_t const n_unknown = std::count_if(g_entries.begin(), g_entries.end(),
		[](PersonEntry const& e) { return !e.known; });
	AddMonoString(box, n_unknown == 0 ?
		ST::format("People in sector: {}", g_entries.size()) :
		ST::format("People in sector: {} ({} unknown)", g_entries.size(), n_unknown));
	for (PersonEntry const& e : g_entries)
	{
		AddMonoString(box, e.name);
		AddSecondColumnMonoString(box, e.position);
	}

	SetBoxFont(                     box, BLOCKFONT2);
	SetBoxForeground(               box, FONT_YELLOW);
	SetBoxBackground(               box, FONT_BLACK);
	SetBoxHighLight(                box, FONT_WHITE);
	SetBoxShade(                    box, FONT_BLACK);
	SetBoxSecondColumnForeground(   box, FONT_WHITE);
	SetBoxSecondColumnBackground(   box, FONT_BLACK);
	SetBoxSecondColumnHighLight(    box, FONT_WHITE);
	SetBoxSecondColumnShade(        box, FONT_BLACK);
	SetBoxSecondColumnMinimumOffset(box, 20);

	SetBoxLineForeground(box, 0, FONT_LTGREEN);
	for (size_t i = 0; i < g_entries.size(); ++i)
	{
		if (!g_entries[i].known)
		{
			SetBoxLineForeground(box, FIRST_ENTRY_LINE + (INT32)i, FONT_GRAY2);
		}
	}

	ResizeBoxToText(box);

	// Center the box in the tactical viewport
	SGPBox const& area = GetBoxArea(box);
	INT16 const x = std::max(0, (SCREEN_WIDTH - area.w) / 2);
	INT16 const y = std::max(0, (gsVIEWPORT_WINDOW_END_Y - area.h) / 2);
	SetBoxXY(box, x, y);

	ShowBox(box);

	MSYS_DefineRegion(&g_mask_region, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
		MSYS_PRIORITY_HIGHEST - 1, MSYS_NO_CURSOR, MaskMoveCallback, MaskButtonCallback);
	g_mask_region.ChangeCursor(0);

	SetUIKeyboardHook(KeyboardHook);
	gfIgnoreScrolling = TRUE;
}


void ToggleSectorPeopleList()
{
	if (IsSectorPeopleListShown())
	{
		RemoveSectorPeopleList();
		return;
	}

	GatherEntries();
	if (g_entries.empty())
	{
		ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, "No one of note in this sector.");
		return;
	}
	CreateBox();
}


void RemoveSectorPeopleList()
{
	if (!g_box) return;

	MSYS_RemoveRegion(&g_mask_region);
	if (gUIKeyboardHook == KeyboardHook) SetUIKeyboardHook(nullptr);
	gfIgnoreScrolling = FALSE;

	RemoveBox(g_box);
	g_box = NO_POPUP_BOX;
	DeleteVideoObject(g_border);
	g_border = nullptr;
	DeleteVideoSurface(g_background);
	g_background = nullptr;
	g_entries.clear();

	// Redraw what was under the box
	SetRenderFlags(RENDER_FLAG_FULL);
}
