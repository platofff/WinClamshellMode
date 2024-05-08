#include <windows.h>
#include <powrprof.h>
#include <shlobj.h>
#include <stdio.h>
#include <io.h>

#pragma comment(lib, "powrprof.lib")

#define ID_TRAY_EXIT 1001
#define ID_TRAY_SET_DISPLAY 1002
#define ID_COMBOBOX 1003
#define ID_OK_BUTTON 1004
#define WINDOW_NAME L"Disable sleep when lid closed with external display connected"
#define INI_FILE_NAME L"ClamshellMode.ini"
#define INI_KEY_NAME L"InternalDisplayID"

static WCHAR INI_PATH[MAX_PATH] = L"";
static WCHAR INTERNAL_DISPLAY_ID[MAX_PATH] = L"";

static void LoadDisplayIDFromIni() {
	GetPrivateProfileStringW(L"Settings", INI_KEY_NAME, L"", INTERNAL_DISPLAY_ID, MAX_PATH, INI_PATH);
}

static BOOL IsDisplayDeviceActive(const WCHAR* deviceID) {
	DISPLAY_DEVICE device = { .cb = sizeof(DISPLAY_DEVICE) };
	DWORD i = 0;

	while (EnumDisplayDevicesW(NULL, i++, &device, 0) != 0) {
		DISPLAY_DEVICE device2 = { .cb = sizeof(DISPLAY_DEVICE) };
		DWORD j = 0;

		while (EnumDisplayDevicesW(device.DeviceName, j++, &device2, 0) != 0) {
			if (wcscmp(device2.DeviceID, deviceID) == 0) {
				return (device2.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0;
			}
		}
	}

	return FALSE;
}

static DWORD SaveDisplayIDToIni(const WCHAR* displayID) {
	if (!IsDisplayDeviceActive(displayID)) {
		return 1;
	}
	wcscpy_s(INTERNAL_DISPLAY_ID, sizeof(INTERNAL_DISPLAY_ID) / sizeof(WCHAR), displayID);
	WritePrivateProfileStringW(L"Settings", INI_KEY_NAME, INTERNAL_DISPLAY_ID, INI_PATH);
	return 0;
}

static HRESULT SetINIPath() {
	HRESULT r = 0;
	if (SUCCEEDED(r = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, INI_PATH))) {
		wcscat_s(INI_PATH, MAX_PATH, L"\\");
		wcscat_s(INI_PATH, MAX_PATH, INI_FILE_NAME);
	}
	return r;
}

static GUID* GetActivePowerScheme() {
	GUID* activeScheme = NULL;
	if (PowerGetActiveScheme(NULL, &activeScheme) != ERROR_SUCCESS) {
		printf("Failed to get active power scheme.\n");
		return NULL;
	}
	return activeScheme;
}

static void _SetLidAction(GUID* scheme, const DWORD newVal) {
	GUID subgroupGUID = GUID_SYSTEM_BUTTON_SUBGROUP;
	GUID powerSettingGUID = GUID_LIDCLOSE_ACTION;

	// Set DC value index
	if (PowerWriteDCValueIndex(NULL, scheme, &subgroupGUID, &powerSettingGUID, newVal) != ERROR_SUCCESS) {
		printf("Failed to set DC value index.\n");
	}

	// Set AC value index
	if (PowerWriteACValueIndex(NULL, scheme, &subgroupGUID, &powerSettingGUID, newVal) != ERROR_SUCCESS) {
		printf("Failed to set AC value index.\n");
	}

	// Apply changes
	if (PowerSetActiveScheme(NULL, scheme) != ERROR_SUCCESS) {
		printf("Failed to set active power scheme.\n");
	}

	printf("Lid action set successfully.\n");
}

static void SetLidAction(const BOOL sleep) {
	GUID* activeScheme = GetActivePowerScheme();
	if (activeScheme != NULL) {
		_SetLidAction(activeScheme, sleep);
		LocalFree(activeScheme);
	}
}

static BOOL ExternalDisplayConnected(const WCHAR* internalDisplayDeviceID) {
	DISPLAY_DEVICE device = { .cb = sizeof(DISPLAY_DEVICE) };
	DWORD i = 0;

	while (EnumDisplayDevicesW(NULL, i++, &device, 0) != 0) {
		DISPLAY_DEVICE device2 = { .cb = sizeof(DISPLAY_DEVICE) };
		DWORD j = 0;

		while (EnumDisplayDevicesW(device.DeviceName, j++, &device2, 0) != 0) {
			if (wcscmp(device2.DeviceID, internalDisplayDeviceID) == 0) {
				continue;
			}
			if (device2.StateFlags & DISPLAY_DEVICE_ACTIVE) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

static void PopulateDisplayDevices(HWND hComboBox) {
	DISPLAY_DEVICE device = { .cb = sizeof(DISPLAY_DEVICE) };
	DWORD i = 0;
	while (EnumDisplayDevicesW(NULL, i++, &device, 0) != 0) {
		DISPLAY_DEVICE device2 = { .cb = sizeof(DISPLAY_DEVICE) };
		DWORD j = 0;
		while (EnumDisplayDevicesW(device.DeviceName, j++, &device2, 0) != 0) {
			if (device2.StateFlags & DISPLAY_DEVICE_ACTIVE) {
				SendMessageW(hComboBox, CB_ADDSTRING, 0, (LPARAM)device2.DeviceID);
			}
		}
	}
}

static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static NOTIFYICONDATA nid = { 0 };
	static HWND hComboBox = NULL;
	static HICON hIcon = NULL;  // Icon handle for reuse

	switch (msg) {
	case WM_CREATE:
		hIcon = LoadIconW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(284));
		nid.cbSize = sizeof(NOTIFYICONDATA);
		nid.hWnd = hwnd;
		nid.uID = 1;
		nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		nid.uCallbackMessage = WM_APP + 1;
		nid.hIcon = hIcon;
		wcscpy_s(nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]), WINDOW_NAME);
		Shell_NotifyIconW(NIM_ADD, &nid);
		LoadDisplayIDFromIni(); // Load the display ID on startup

		// Check if the INI file exists, if not, prompt for display selection
		WCHAR iniPath[MAX_PATH] = L"";
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, iniPath))) {
			wcscat_s(iniPath, MAX_PATH, L"\\");
			wcscat_s(iniPath, MAX_PATH, INI_FILE_NAME);
			if (_waccess(iniPath, 0) == -1) {
				PostMessageW(hwnd, WM_COMMAND, ID_TRAY_SET_DISPLAY, 0);
			}

		}
		break;

	case WM_APP + 1:
		if (LOWORD(lParam) == WM_RBUTTONDOWN) {
			POINT cursor;
			GetCursorPos(&cursor);
			HMENU hMenu = CreatePopupMenu();
			AppendMenuW(hMenu, MF_STRING, ID_TRAY_SET_DISPLAY, L"Select internal display...");
			AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
			SetForegroundWindow(hwnd);
			TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd, NULL);
			DestroyMenu(hMenu);
		}
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == ID_TRAY_EXIT) {
			PostQuitMessage(0);
		}
		else if (LOWORD(wParam) == ID_TRAY_SET_DISPLAY) {
			HWND hDisplayWindow = CreateWindowExW(0, L"DISPLAY_WINDOW_CLASS", L"Select Internal Display",
				WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 400, 130,
				NULL, NULL, GetModuleHandleW(NULL), NULL);
			SendMessageW(hDisplayWindow, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);  // Set the small icon
			SendMessageW(hDisplayWindow, WM_SETICON, ICON_BIG, (LPARAM)hIcon);    // Set the big icon
			ShowWindow(hDisplayWindow, SW_SHOW);
		}
		else if (LOWORD(wParam) == ID_COMBOBOX && HIWORD(wParam) == CBN_SELCHANGE) {
			WPARAM index = SendMessageW(hComboBox, CB_GETCURSEL, 0, 0);
			SendMessageW(hComboBox, CB_GETLBTEXT, index, (LPARAM)INTERNAL_DISPLAY_ID);
		}
		break;

	case WM_DISPLAYCHANGE:
		if (ExternalDisplayConnected(INTERNAL_DISPLAY_ID)) {
			SetLidAction(FALSE);
		}
		else {
			SetLidAction(TRUE);
		}
		break;

	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &nid);
		if (hIcon) {
			DestroyIcon(hIcon);  // Clean up the icon
		}
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	return 0;
}

static LRESULT CALLBACK DisplayWindowProcedure(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static HWND hComboBox = NULL;
	static WCHAR currentDisplayID[MAX_PATH] = L"";

	switch (msg) {
	case WM_CREATE:
		hComboBox = CreateWindowW(L"COMBOBOX", NULL,
			CBS_DROPDOWN | WS_CHILD | WS_VISIBLE,
			10, 10, 360, 120, hwnd, (HMENU)ID_COMBOBOX,
			GetModuleHandleW(NULL), NULL);
		PopulateDisplayDevices(hComboBox);

		CreateWindowW(L"BUTTON", L"OK",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			140, 50, 100, 30, hwnd, (HMENU)ID_OK_BUTTON,
			GetModuleHandleW(NULL), NULL);
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == ID_COMBOBOX && HIWORD(wParam) == CBN_SELCHANGE) {
			WPARAM index = SendMessageW(hComboBox, CB_GETCURSEL, 0, 0);
			SendMessageW(hComboBox, CB_GETLBTEXT, index, (LPARAM)currentDisplayID);
		}
		else if (LOWORD(wParam) == ID_OK_BUTTON) {
			if (SaveDisplayIDToIni(currentDisplayID) == 0) {
				currentDisplayID[0] = L'\0';
				ShowWindow(hwnd, SW_HIDE);  // Hide the window instead of destroying it
			}
			else {
				MessageBoxW(hwnd, L"Display not found!", L"Error", MB_ICONERROR | MB_OK);
			}
		}
		break;

	case WM_SIZE:
		InvalidateRect(hwnd, NULL, TRUE);
		break;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
		EndPaint(hwnd, &ps);
		break;
	}

	case WM_DESTROY:
		if (wcslen(INTERNAL_DISPLAY_ID) == 0) {
			PostQuitMessage(0); // Exit only if INI file does not exist
		}
		else {
			ShowWindow(hwnd, SW_HIDE); // Otherwise, just hide the window
		}
		break;

	case WM_DISPLAYCHANGE:
		SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
		PopulateDisplayDevices(hComboBox);
		break;

	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	HANDLE mutex = CreateMutexW(NULL, TRUE, L"Global\\ClamshellMode");
	if (GetLastError() == ERROR_ALREADY_EXISTS || mutex == NULL) {
		return 1;
	}

	const WCHAR className[] = L"ClamshellModeWindow";
	const WCHAR displayClassName[] = L"DISPLAY_WINDOW_CLASS";

	if (!SUCCEEDED(SetINIPath())) {
		return 1;
	}

	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = WindowProcedure;
	wc.hInstance = hInstance;
	wc.lpszClassName = className;
	RegisterClassW(&wc);

	wc.lpfnWndProc = DisplayWindowProcedure;
	wc.lpszClassName = displayClassName;
	wc.hIcon = LoadIconW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(284));  // Set the icon for display window
	RegisterClassW(&wc);

	HWND hwnd = CreateWindowExW(0, className, WINDOW_NAME,
		0, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, NULL, NULL, hInstance, NULL);

	MSG msg = { 0 };
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	CloseHandle(mutex);
	return 0;
}

int main(void) {
	return WinMain(GetModuleHandleW(NULL), NULL, NULL, 0);
}
