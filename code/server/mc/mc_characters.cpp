// Character choice (Minecraft characters or any Counter-Strike model): the menu, the command, and
// telling every client who looks like what. Bots get a random mix.
#include "precompiled.h"

#include "mc_chars.h"
#include "mc_server.h"

namespace mc
{
static int msgChar = 0;
static int g_char[MAX_CLIENTS + 1];

void ShowMenuText(CBasePlayer* pl, int keys, const char* text); // mc_main.cpp

void CharactersInit() { msgChar = REG_USER_MSG(MCMSG_CHAR, 2); }

static void Broadcast(int index, edict_t* only = nullptr)
{
	if (!msgChar)
		return;
	if (only)
		MESSAGE_BEGIN(MSG_ONE, msgChar, nullptr, only);
	else
		MESSAGE_BEGIN(MSG_ALL, msgChar);
	WRITE_BYTE(index);
	WRITE_BYTE(g_char[index]);
	MESSAGE_END();
}

// Counter-Strike side of the look: the chosen model, or the team's own again
static void ApplyModel(CBasePlayer* pl)
{
	int c = g_char[pl->entindex()];
	const char* m = (c > 0 && c < mcp::kNumCharacters) ? mcp::kCharacters[c].csModel : nullptr;
	Q_strlcpy(pl->CSPlayer()->m_szModel, m ? m : "");
	if (pl->m_iTeam == CT || pl->m_iTeam == TERRORIST)
		pl->SetPlayerModel(pl->m_bHasC4);
}

void SetCharacter(CBasePlayer* pl, int c)
{
	if (c < 0 || c >= mcp::kNumCharacters)
		return;
	g_char[pl->entindex()] = c;
	ApplyModel(pl);
	Broadcast(pl->entindex());
	if (!pl->IsBot())
		Toast(pl, 0, "You are now %s", mcp::kCharacters[c].display);
	McLog("character: %s is %s", STRING(pl->pev->netname), mcp::kCharacters[c].name);
}

int CharacterOf(CBasePlayer* pl) { return g_char[pl->entindex()]; }

void CharacterSpawn(CBasePlayer* pl)
{
	int i = pl->entindex();
	static bool assigned[MAX_CLIENTS + 1];
	if (IsMobBot(pl))
	{
		// a team mob has a character of its own, whatever look the slot's last bot had
		assigned[i] = false;
		g_char[i] = TeamMobOf(pl) == TM_WITHER ? mcp::CHAR_FIRST_MOB + 1 : mcp::CHAR_FIRST_MOB;
		Broadcast(i);
		ApplyModel(pl);
		return;
	}
	if (pl->IsBot() && !assigned[i])
	{
		// bots: a mix of looks; the mob bots keep their mob
		assigned[i] = true;
		const char* n = STRING(pl->pev->netname);
		int c = 0;
		if (strstr(n, "Creeper"))
			c = 6;
		else if (strstr(n, "Enderman"))
			c = 7;
		else if (RANDOM_LONG(0, 1))
			c = RANDOM_LONG(mcp::CHAR_FIRST_MC, mcp::CHAR_FIRST_CS - 1);
		g_char[i] = c;
		Broadcast(i);
	}
	ApplyModel(pl);
}

void CharacterDisconnect(int index) { g_char[index] = 0; }

// a client joined: tell it everyone's look
void SendCharacters(edict_t* to)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
		if (g_char[i])
			Broadcast(i, to);
}

// menus 30 (groups) and 31..33 (the characters of a group)
void OpenCharacterMenu(CBasePlayer* pl, int menu)
{
	McPlayer& mp = P(pl);
	mp.menu = menu;
	mp.menuPage = 0;
	char buf[512];
	int keys = 1 << 9;
	if (menu == 30)
	{
		Q_strlcpy(buf, "\\yChoose your character\\w\n\n1. Minecraft characters\n2. Minecraft player skins\n3. Counter-Strike models\n4. Team default\n\n0. Close");
		keys |= 0xF;
	}
	else
	{
		int first = menu == 31 ? mcp::CHAR_FIRST_MC : menu == 32 ? mcp::CHAR_FIRST_SKIN : mcp::CHAR_FIRST_CS;
		int last = menu == 31 ? mcp::CHAR_FIRST_SKIN : menu == 32 ? mcp::CHAR_FIRST_CS : mcp::CHAR_FIRST_MOB;
		Q_snprintf(buf, sizeof(buf), "\\y%s\\w\n\n", menu == 31 ? "Minecraft characters" : menu == 32 ? "Minecraft player skins" : "Counter-Strike models");
		for (int c = first, k = 0; c < last && k < 8; c++, k++)
		{
			char line[64];
			Q_snprintf(line, sizeof(line), "%d. %s%s\n", k + 1, mcp::kCharacters[c].display, g_char[pl->entindex()] == c ? " \\r(you)\\w" : "");
			Q_strlcat(buf, line);
			keys |= 1 << k;
		}
		Q_strlcat(buf, "\n9. Back\n0. Close");
		keys |= 1 << 8;
	}
	ShowMenuText(pl, keys, buf);
}

bool CharacterMenuSelect(CBasePlayer* pl, int menu, int key)
{
	if (menu == 30)
	{
		if (key >= 1 && key <= 3)
			OpenCharacterMenu(pl, 30 + key);
		else if (key == 4)
			SetCharacter(pl, 0);
		return true;
	}
	if (menu >= 31 && menu <= 33)
	{
		if (key == 9)
		{
			OpenCharacterMenu(pl, 30);
			return true;
		}
		int first = menu == 31 ? mcp::CHAR_FIRST_MC : menu == 32 ? mcp::CHAR_FIRST_SKIN : mcp::CHAR_FIRST_CS;
		int last = menu == 31 ? mcp::CHAR_FIRST_SKIN : menu == 32 ? mcp::CHAR_FIRST_CS : mcp::CHAR_FIRST_MOB;
		int c = first + key - 1;
		if (key >= 1 && c < last)
			SetCharacter(pl, c);
		return true;
	}
	return false;
}

// "mc_character" opens the menu; "mc_character <name>" picks directly
bool CharacterCommand(CBasePlayer* pl, const char* arg)
{
	if (!arg || !*arg)
	{
		OpenCharacterMenu(pl, 30);
		return true;
	}
	for (int c = 0; c < mcp::CHAR_FIRST_MOB; c++)
		if (!Q_stricmp(arg, mcp::kCharacters[c].name))
		{
			SetCharacter(pl, c);
			return true;
		}
	Toast(pl, 0, "Unknown character %s", arg);
	return true;
}
} // namespace mc
