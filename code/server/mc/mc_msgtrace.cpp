// Debug aid (mc_msgtrace 1): log the user messages the server sends to human players, with the first
// byte/short written, so join/menu flows can be compared message by message.
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
static cvar_t cv_msgtrace = {"mc_msgtrace", "0", FCVAR_SERVER, 0.0f, nullptr};
static char g_msgNames[256][16];
static int (*o_RegUserMsg)(const char*, int);
static void (*o_MessageBegin)(int, int, const float*, edict_t*);
static void (*o_WriteByte)(int);
static void (*o_WriteShort)(int);
static void (*o_WriteString)(const char*);
static bool g_tracing;
static int g_traceArgs;
static char g_traceLine[256];

static int W_RegUserMsg(const char* name, int size)
{
	int id = o_RegUserMsg(name, size);
	if (id > 0 && id < 256)
		Q_strlcpy(g_msgNames[id], name);
	return id;
}

static void Flush()
{
	if (g_tracing)
		McLog("msg %s", g_traceLine);
	g_tracing = false;
}

static void W_MessageBegin(int dest, int type, const float* origin, edict_t* ed)
{
	Flush();
	if (cv_msgtrace.value != 0.0f && type > 0 && type < 256 && g_msgNames[type][0])
	{
		bool toHuman = ed && !(ed->v.flags & FL_FAKECLIENT);
		bool toAll = dest == MSG_ALL || dest == MSG_BROADCAST;
		if (toHuman || toAll)
		{
			g_tracing = true;
			g_traceArgs = 0;
			Q_snprintf(g_traceLine, sizeof(g_traceLine), "%s -> %s%s", g_msgNames[type], toAll ? "all" : STRING(ed->v.netname),
				dest == MSG_ONE_UNRELIABLE ? " (unreliable)" : "");
		}
	}
	o_MessageBegin(dest, type, origin, ed);
}

static void Append(const char* s)
{
	if (g_tracing && g_traceArgs++ < 4)
		Q_strlcat(g_traceLine, s);
}

static void W_WriteByte(int v)
{
	char b[16];
	Q_snprintf(b, sizeof(b), " %d", v);
	Append(b);
	o_WriteByte(v);
}

static void W_WriteShort(int v)
{
	char b[16];
	Q_snprintf(b, sizeof(b), " s%d", v);
	Append(b);
	o_WriteShort(v);
}

static void W_WriteString(const char* s)
{
	char b[40];
	Q_snprintf(b, sizeof(b), " '%.30s'", s ? s : "");
	Append(b);
	o_WriteString(s);
}

static void W_MessageEnd();
static void (*o_MessageEnd)();
static void W_MessageEnd()
{
	Flush();
	o_MessageEnd();
}

void InstallMsgTrace()
{
	CVAR_REGISTER(&cv_msgtrace);
	o_RegUserMsg = g_engfuncs.pfnRegUserMsg;
	o_MessageBegin = g_engfuncs.pfnMessageBegin;
	o_MessageEnd = g_engfuncs.pfnMessageEnd;
	o_WriteByte = g_engfuncs.pfnWriteByte;
	o_WriteShort = g_engfuncs.pfnWriteShort;
	o_WriteString = g_engfuncs.pfnWriteString;
	g_engfuncs.pfnRegUserMsg = W_RegUserMsg;
	g_engfuncs.pfnMessageBegin = W_MessageBegin;
	g_engfuncs.pfnMessageEnd = W_MessageEnd;
	g_engfuncs.pfnWriteByte = W_WriteByte;
	g_engfuncs.pfnWriteShort = W_WriteShort;
	g_engfuncs.pfnWriteString = W_WriteString;
}
} // namespace mc
