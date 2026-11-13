/*
 * corvus-rtl-driver: give RTL2832U dongles Windows' own WinUSB driver, so librtlsdr (and Corvus Sweep) can open
 * them. What Zadig does for these dongles, without its window.
 *
 *   corvus-rtl-driver.exe [--result FILE] [--force]
 *
 * Needs Windows 8 or later (WinUSB is part of Windows there) and administrator rights (the manifest asks for
 * them). It writes a driver package (an INF that points at Windows' WinUSB for every dongle librtlsdr knows),
 * makes a catalog for it and signs that with a certificate made for the purpose (libwdi's pki.c: the
 * certificate goes into the machine's Root and TrustedPublisher stores and its private key is deleted, so it
 * can sign nothing else), stages the package in the driver store, and installs it on every dongle plugged in.
 * Dongles already on WinUSB are left alone unless --force.
 *
 * Exit status: 0 installed (or already on WinUSB), 2 no dongle plugged in (the package is staged anyway, and
 * a dongle plugged in later gets it if Windows has nothing better), 1 failed. --result FILE gets one line per
 * step, the last one "OK ...", "NODEVICE ..." or "ERROR ...": an elevated process's output isn't visible to
 * the program that started it.
 *
 * Copyright (c) 2026 the Corvus authors. LGPL-3.0-or-later (it links libwdi's pki.c and logging.c).
 */

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ids.h" /* RTL_IDS: { vid, pid, "name" }, generated from librtlsdr's known_devices */

BOOL CreateCat(LPCSTR szCatPath, LPCSTR szHWID, LPCSTR szSearchDir, LPCSTR* szFileList, DWORD cFileList);
BOOL SelfSignFile(LPCSTR szFileName, LPCSTR szCertSubject);

#define INF_NAME "corvus-rtl.inf"
#define CAT_NAME "corvus-rtl.cat"
#define CERT_SUBJECT "CN=Corvus RTL WinUSB driver (self-signed, key deleted)"
/* DeviceInterfaceGUIDs: the package's own (libusb finds WinUSB devices without needing a particular one) */
#define INTERFACE_GUID "{5C3E4B7A-2D41-4F5B-9C0E-6A1D8B2F7E31}"
#ifndef DRIVER_VERSION
#define DRIVER_VERSION "1.0.0.0"
#endif

static FILE *result;

static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	if (result) {
		va_start(ap, fmt);
		vfprintf(result, fmt, ap);
		va_end(ap);
		fputc('\n', result);
		fflush(result);
	}
}

static int known(const char *hwid, char *out, size_t n)
{
	/* hwid like USB\VID_0BDA&PID_2838&REV_0100&MI_00: is it one of ours (interface 0, or the whole device)? */
	unsigned vid, pid;
	const char *mi = strstr(hwid, "&MI_");
	if (sscanf(hwid, "USB\\VID_%4x&PID_%4x", &vid, &pid) != 2 && sscanf(hwid, "usb\\vid_%4x&pid_%4x", &vid, &pid) != 2)
		return 0;
	if (mi != NULL && strncmp(mi, "&MI_00", 6) != 0)
		return 0;
	for (size_t i = 0; i < sizeof(RTL_IDS) / sizeof(RTL_IDS[0]); i++) {
		if (RTL_IDS[i].vid == vid && RTL_IDS[i].pid == pid) {
			snprintf(out, n, mi ? "USB\\VID_%04X&PID_%04X&MI_00" : "USB\\VID_%04X&PID_%04X", vid, pid);
			return 1;
		}
	}
	return 0;
}

static int write_inf(const char *path)
{
	FILE *f = fopen(path, "w");
	SYSTEMTIME t;
	if (f == NULL)
		return 0;
	GetSystemTime(&t);
	fprintf(f,
		"; corvus-rtl: RTL2832U dongles on Windows' WinUSB (Windows 8 and later)\n"
		"[Version]\n"
		"Signature   = \"$Windows NT$\"\n"
		"Class       = USBDevice\n"
		"ClassGuid   = {88bae032-5a81-49f0-bc3d-a4ff138216d6}\n"
		"Provider    = %%Provider%%\n"
		"CatalogFile = " CAT_NAME "\n"
		"DriverVer   = %02d/%02d/%04d, " DRIVER_VERSION "\n\n"
		"[Manufacturer]\n"
		"%%Provider%% = Rtl, NTamd64, NTarm64, NTx86\n\n",
		t.wMonth, t.wDay, t.wYear);
	const char *sect[] = { "Rtl.NTamd64", "Rtl.NTarm64", "Rtl.NTx86" };
	for (int s = 0; s < 3; s++) {
		fprintf(f, "[%s]\n", sect[s]);
		for (size_t i = 0; i < sizeof(RTL_IDS) / sizeof(RTL_IDS[0]); i++) {
			fprintf(f, "%%DeviceName%% = USB_Install, USB\\VID_%04X&PID_%04X\n", RTL_IDS[i].vid, RTL_IDS[i].pid);
			fprintf(f, "%%DeviceName%% = USB_Install, USB\\VID_%04X&PID_%04X&MI_00\n", RTL_IDS[i].vid, RTL_IDS[i].pid);
		}
		fprintf(f, "\n");
	}
	fprintf(f,
		"[USB_Install]\n"
		"Include = winusb.inf\n"
		"Needs   = WINUSB.NT\n\n"
		"[USB_Install.Services]\n"
		"Include = winusb.inf\n"
		"Needs   = WINUSB.NT.Services\n\n"
		"[USB_Install.HW]\n"
		"AddReg = Dev_AddReg\n\n"
		"[Dev_AddReg]\n"
		"HKR,,DeviceInterfaceGUIDs,0x10000,\"" INTERFACE_GUID "\"\n\n"
		"[Strings]\n"
		"Provider   = \"Corvus RTL\"\n"
		"DeviceName = \"RTL2832U SDR (WinUSB, Corvus RTL)\"\n");
	return fclose(f) == 0;
}

int main(int argc, char **argv)
{
	int force = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--result") == 0 && i + 1 < argc)
			result = fopen(argv[++i], "w");
		else if (strcmp(argv[i], "--force") == 0)
			force = 1;
		else {
			printf("usage: corvus-rtl-driver [--result FILE] [--force]\n");
			return 1;
		}
	}

	/* the dongles plugged in, and the driver each has now */
	char ids[16][64];
	int n = 0, on_winusb = 0;
	HDEVINFO set = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (set == INVALID_HANDLE_VALUE) {
		say("ERROR can't list USB devices (%lu)", GetLastError());
		return 1;
	}
	SP_DEVINFO_DATA dev = { .cbSize = sizeof(dev) };
	for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); i++) {
		char hw[2048] = { 0 }, svc[256] = { 0 }, id[64];
		if (!SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_HARDWAREID, NULL, (BYTE*)hw, sizeof(hw) - 2, NULL))
			continue;
		for (char *p = hw; *p; p += strlen(p) + 1) {
			if (!known(p, id, sizeof(id)))
				continue;
			SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_SERVICE, NULL, (BYTE*)svc, sizeof(svc) - 1, NULL);
			int winusb = _stricmp(svc, "WinUSB") == 0;
			say("FOUND %s (driver: %s)", id, svc[0] ? svc : "none");
			on_winusb += winusb;
			if ((!winusb || force) && n < 16) {
				int dup = 0;
				for (int k = 0; k < n; k++)
					dup |= strcmp(ids[k], id) == 0;
				if (!dup)
					strcpy(ids[n++], id);
			}
			break;
		}
	}
	SetupDiDestroyDeviceInfoList(set);
	if (n == 0 && on_winusb > 0) {
		say("OK already on WinUSB");
		return 0;
	}

	/* the package: INF, catalog, signature */
	char dir[MAX_PATH], inf[MAX_PATH], cat[MAX_PATH];
	DWORD k = GetTempPathA(sizeof(dir), dir);
	if (k == 0 || k > sizeof(dir) - 32) {
		say("ERROR no temporary folder");
		return 1;
	}
	strcat(dir, "corvus-rtl-driver");
	CreateDirectoryA(dir, NULL);
	snprintf(inf, sizeof(inf), "%s\\" INF_NAME, dir);
	snprintf(cat, sizeof(cat), "%s\\" CAT_NAME, dir);
	if (!write_inf(inf)) {
		say("ERROR can't write %s", inf);
		return 1;
	}
	LPCSTR files[] = { INF_NAME };
	if (!CreateCat(cat, "USB\\VID_0BDA&PID_2838", dir, files, 1)) {
		say("ERROR can't make the driver catalog");
		return 1;
	}
	if (!SelfSignFile(cat, CERT_SUBJECT)) {
		say("ERROR can't sign the driver catalog");
		return 1;
	}
	say("SIGNED %s", cat);
	char staged[MAX_PATH] = { 0 };
	if (!SetupCopyOEMInfA(inf, NULL, SPOST_PATH, 0, staged, sizeof(staged), NULL, NULL)) {
		say("ERROR can't add the driver to the driver store (%lu)", GetLastError());
		return 1;
	}
	say("STAGED %s", staged);
	if (n == 0) {
		say("NODEVICE no RTL2832U dongle plugged in; the driver is staged for when one is");
		return 2;
	}

	/* and on each dongle plugged in */
	int fails = 0;
	for (int i = 0; i < n; i++) {
		BOOL reboot = FALSE;
		if (UpdateDriverForPlugAndPlayDevicesA(NULL, ids[i], inf, INSTALLFLAG_FORCE, &reboot))
			say("INSTALLED %s%s", ids[i], reboot ? " (restart Windows to finish)" : "");
		else {
			say("FAILED %s (%lu)", ids[i], GetLastError());
			fails++;
		}
	}
	if (fails) {
		say("ERROR %d of %d dongles didn't take the driver", fails, n);
		return 1;
	}
	say("OK installed on %d dongle%s", n, n == 1 ? "" : "s");
	return 0;
}
