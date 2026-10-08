// Characters a player can pick (Shift+M / mc_character): Minecraft characters drawn by the mod, or
// Counter-Strike's own player models, from either team. 0 = the team's default.
#pragma once

namespace mcp
{
struct Character
{
	const char* name;    // command name
	const char* display; // menu text
	const char* csModel; // Counter-Strike player model (models/player/<csModel>), or nullptr
	const char* skin;    // Minecraft texture the mod draws the player with, or nullptr
};

static const Character kCharacters[] = {
	{"default", "Team default", nullptr, nullptr},
	// Minecraft characters
	{"steve", "Steve", nullptr, "entity/player/wide/steve"},
	{"alex", "Alex", nullptr, "entity/player/slim/alex"},
	{"zombie", "Zombie", nullptr, "entity/zombie/zombie"},
	{"husk", "Husk", nullptr, "entity/zombie/husk"},
	{"drowned", "Drowned", nullptr, "entity/zombie/drowned"},
	{"creeper", "Creeper", nullptr, "entity/creeper/creeper"},
	{"enderman", "Enderman", nullptr, "entity/enderman/enderman"},
	// Minecraft's other default player skins
	{"ari", "Ari", nullptr, "entity/player/wide/ari"},
	{"efe", "Efe", nullptr, "entity/player/wide/efe"},
	{"kai", "Kai", nullptr, "entity/player/wide/kai"},
	{"makena", "Makena", nullptr, "entity/player/wide/makena"},
	{"noor", "Noor", nullptr, "entity/player/wide/noor"},
	{"sunny", "Sunny", nullptr, "entity/player/wide/sunny"},
	{"zuri", "Zuri", nullptr, "entity/player/wide/zuri"},
	// Counter-Strike models (any team)
	{"urban", "SEAL Team 6", "urban", nullptr},
	{"gsg9", "GSG-9", "gsg9", nullptr},
	{"sas", "SAS", "sas", nullptr},
	{"gign", "GIGN", "gign", nullptr},
	{"terror", "Phoenix Connexion", "terror", nullptr},
	{"leet", "Elite Crew", "leet", nullptr},
	{"arctic", "Arctic Avengers", "arctic", nullptr},
	{"guerilla", "Guerilla Warfare", "guerilla", nullptr},
};
static const int kNumCharacters = (int)(sizeof(kCharacters) / sizeof(kCharacters[0]));
static const int CHAR_FIRST_MC = 1, CHAR_FIRST_SKIN = 8, CHAR_FIRST_CS = 15;

inline bool IsMinecraftCharacter(int c) { return c > 0 && c < kNumCharacters && kCharacters[c].skin != nullptr; }
} // namespace mcp

#define MCMSG_CHAR "McChar" // byte player index, byte character (mcp::kCharacters)
