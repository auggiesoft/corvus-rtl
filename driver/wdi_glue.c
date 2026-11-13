/*
 * What libwdi's pki.c and logging.c take from libwdi.c, for corvus-rtl-driver (which uses only those two).
 * Windows 8 or later is required (WinUSB is part of Windows there), so the version check pki.c makes, to choose
 * SHA-256 over SHA-1 for the certificate, always lands on SHA-256. LGPL-3.0-or-later.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "stdfn.h"

int nWindowsVersion = WINDOWS_10;

void GetWindowsVersion(void)
{
	nWindowsVersion = WINDOWS_10;
}

char *wdi_windows_error_str(uint32_t retval)
{
	static char msg[256];
	DWORD code = retval ? retval : GetLastError();
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg, sizeof(msg), NULL);
	if (n == 0)
		snprintf(msg, sizeof(msg), "Windows error 0x%08lX", (unsigned long)code);
	while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r'))
		msg[--n] = 0;
	return msg;
}
