#ifndef SECTOR_PEOPLE_LIST_H
#define SECTOR_PEOPLE_LIST_H

// Popup listing the important (profiled) characters in the loaded sector.
// Characters the player's mercs have never seen are listed as "Unknown",
// except shopkeepers which are always named. Clicking an entry centers the
// view on that character.

void ToggleSectorPeopleList();
void RemoveSectorPeopleList();
bool IsSectorPeopleListShown();

#endif
