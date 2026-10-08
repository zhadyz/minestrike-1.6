// Minecraft music through the engine's MP3 player (hw.dll: "mp3 playfile", volume = MP3Volume).
// Tracks and lengths come from mc/music/tracks.txt (tools/assets/fetch_music.py): "<pool> <track>
// <weight> <seconds>". Like Minecraft's MusicManager, a weighted random track from the situation's pool
// (desert for Dust II, creative in creative mode), never the same track twice in a row, then silence
// for a random while. Minecraft waits 10-20 minutes between songs; the default here is shorter
// (mc_music_mindelay / mc_music_maxdelay, seconds).
#include "hlsdk_client.h"
#include "mc_client.h"
#include "mc_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace mc
{
struct Track
{
	std::string pool, name;
	int weight;
	float seconds;
};
static std::vector<Track> g_tracks;
static bool g_loaded = false;
static cvar_t* g_cvMusic = nullptr;
static cvar_t* g_cvMin = nullptr;
static cvar_t* g_cvMax = nullptr;
static double g_nextStart = -1.0; // client time the next track starts (-1: not scheduled)
static double g_playingUntil = 0.0;
static std::string g_last;
static bool g_playing = false;

static void LoadTracks()
{
	g_loaded = true;
	int len = 0;
	unsigned char* data = gEngfuncs.COM_LoadFile((char*)"mc/music/tracks.txt", 5, &len);
	if (!data)
	{
		Log("music: no mc/music/tracks.txt (run tools/assets/fetch_music.py)");
		return;
	}
	std::string text((const char*)data, (size_t)len);
	gEngfuncs.COM_FreeFile(data);
	size_t p = 0;
	while (p < text.size())
	{
		size_t e = text.find('\n', p);
		std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
		p = e == std::string::npos ? text.size() : e + 1;
		char pool[32], name[64];
		int w;
		float s;
		if (sscanf(line.c_str(), "%31s %63s %d %f", pool, name, &w, &s) == 4 && s > 1.0f)
			g_tracks.push_back({pool, name, w, s});
	}
	Log("music: %d tracks", (int)g_tracks.size());
}

void MusicPlay(const char* name);
void MusicStop();

static const Track* Pick()
{
	const char* pool = g_cl.creative ? "creative" : "desert";
	int total = 0;
	for (const Track& t : g_tracks)
		if (t.pool == pool && t.name != g_last)
			total += t.weight;
	if (total <= 0)
		return nullptr;
	int r = rand() % total;
	for (const Track& t : g_tracks)
		if (t.pool == pool && t.name != g_last && (r -= t.weight) < 0)
			return &t;
	return nullptr;
}

static float Gap()
{
	float lo = g_cvMin ? g_cvMin->value : 60.0f, hi = g_cvMax ? g_cvMax->value : 240.0f;
	if (hi < lo)
		hi = lo;
	return lo + (hi - lo) * (rand() / (float)RAND_MAX);
}

static void Cmd_MusicPlay() { MusicPlay(gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv(1) : nullptr); }
static void Cmd_MusicStop() { MusicStop(); }

void MusicInit()
{
	gEngfuncs.pfnAddCommand((char*)"mc_music_play", Cmd_MusicPlay);
	gEngfuncs.pfnAddCommand((char*)"mc_music_stop", Cmd_MusicStop);
	g_cvMusic = gEngfuncs.pfnRegisterVariable((char*)"mc_music", (char*)"1", FCVAR_ARCHIVE);
	g_cvMin = gEngfuncs.pfnRegisterVariable((char*)"mc_music_mindelay", (char*)"60", FCVAR_ARCHIVE);
	g_cvMax = gEngfuncs.pfnRegisterVariable((char*)"mc_music_maxdelay", (char*)"240", FCVAR_ARCHIVE);
}

void MusicStop()
{
	if (g_playing)
		gEngfuncs.pfnClientCmd((char*)"mp3 stop\n");
	g_playing = false;
	g_nextStart = -1.0;
}

// Play a track now ("mc_music_play [name]"; random from the current pool without a name).
void MusicPlay(const char* name)
{
	if (!g_loaded)
		LoadTracks();
	const Track* t = nullptr;
	if (name && *name)
	{
		for (const Track& k : g_tracks)
			if (k.name == name)
				t = &k;
	}
	else
		t = Pick();
	if (!t)
		return;
	char cmd[160];
	snprintf(cmd, sizeof(cmd), "mp3 playfile mc/music/%s.mp3\n", t->name.c_str());
	gEngfuncs.pfnClientCmd(cmd);
	g_last = t->name;
	g_playing = true;
	g_playingUntil = g_cl.time + t->seconds;
	g_nextStart = g_playingUntil + Gap();
	Log("music: playing %s (%s, %.0f s), next in %.0f s after it", t->name.c_str(), t->pool.c_str(), t->seconds,
		g_nextStart - g_playingUntil);
}

void MusicFrame()
{
	if (!g_cvMusic)
		return;
	char lvl[64] = "";
	const char* ln = gEngfuncs.pfnGetLevelName ? gEngfuncs.pfnGetLevelName() : "";
	if (ln)
		strncpy(lvl, ln, sizeof(lvl) - 1);
	if (g_cvMusic->value <= 0.0f || !lvl[0] || !g_cl.haveInv)
	{
		if (g_playing && g_cvMusic->value <= 0.0f)
			MusicStop();
		return;
	}
	if (!g_loaded)
		LoadTracks();
	if (g_tracks.empty())
		return;
	if (g_playing && g_cl.time >= g_playingUntil)
		g_playing = false;
	if (g_nextStart < 0.0)
		g_nextStart = g_cl.time + 5.0 + 10.0 * (rand() / (double)RAND_MAX); // Minecraft starts soon after joining
	if (!g_playing && g_cl.time >= g_nextStart)
		MusicPlay(nullptr);
}
} // namespace mc
