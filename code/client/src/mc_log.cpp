#include "mc_client.h"

#include <stdarg.h>
#include <stdio.h>

namespace mc
{
static FILE* g_log = nullptr;

void Log(const char* fmt, ...)
{
	if (!g_log)
	{
		// Next to the mod folder: <Half-Life>\csmc\logs\mc_client.log
		char path[MAX_PATH];
		HMODULE h = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&Log, &h);
		GetModuleFileNameA(h, path, sizeof(path));
		char* s = strrchr(path, '\\'); // ...\csmc\cl_dlls\client.dll
		if (s) *s = 0;
		s = strrchr(path, '\\');
		if (s) *s = 0;
		strcat(path, "\\logs");
		CreateDirectoryA(path, nullptr);
		strcat(path, "\\mc_client.log");
		g_log = fopen(path, "w");
		if (!g_log)
			return;
	}
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);
}
} // namespace mc
