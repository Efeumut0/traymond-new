#include <Windows.h>
#include <windowsx.h>
#include <cstdlib>
#include <string>
#include <vector>

#define VK_Z_KEY 0x5A
// These keys are used to send windows to tray
#define TRAY_KEY VK_Z_KEY
#define MOD_KEY MOD_WIN + MOD_SHIFT

#define WM_ICON 0x1C0A
#define WM_OURICON 0x1C0B
#define EXIT_ID 0x99
#define SHOW_ALL_ID 0x98
#define MAXIMUM_WINDOWS 100
#define MAIN_ICON_ID 0xFFFF
#define FIRST_HIDDEN_ICON_ID 1
#define TRAY_ICON_RETRY_TIMER_ID 1
#define TRAY_ICON_RETRY_INTERVAL_MS 2000

// Stores hidden window record.
typedef struct HIDDEN_WINDOW {
  NOTIFYICONDATA icon;
  HWND window;
} HIDDEN_WINDOW;

// Current execution context
typedef struct TRCONTEXT {
  HWND mainWindow;
  NOTIFYICONDATA mainIcon;
  HIDDEN_WINDOW icons[MAXIMUM_WINDOWS];
  HMENU trayMenu;
  UINT taskbarRestartMessage;
  int iconIndex; // How many windows are currently hidden
  bool mainIconVisible;
} TRCONTEXT;

HANDLE saveFile;

void save(const TRCONTEXT *context);
bool addNotifyIcon(NOTIFYICONDATA *icon);
bool refreshMainTrayIcon(TRCONTEXT *context);
void refreshAllTrayIcons(TRCONTEXT *context);

UINT allocateIconId(const TRCONTEXT *context) {
  for (UINT iconId = FIRST_HIDDEN_ICON_ID; iconId < FIRST_HIDDEN_ICON_ID + MAXIMUM_WINDOWS; iconId++)
  {
    bool iconIdInUse = false;
    for (int i = 0; i < context->iconIndex; i++)
    {
      if (context->icons[i].window && context->icons[i].icon.uID == iconId) {
        iconIdInUse = true;
        break;
      }
    }

    if (!iconIdInUse) {
      return iconId;
    }
  }

  return 0;
}

void removeHiddenWindow(TRCONTEXT *context, int index) {
  for (int i = index; i < context->iconIndex - 1; i++)
  {
    context->icons[i] = context->icons[i + 1];
  }

  if (context->iconIndex > 0) {
    context->icons[context->iconIndex - 1] = {};
    context->iconIndex--;
  }
}

bool addNotifyIcon(NOTIFYICONDATA *icon) {
  if (!Shell_NotifyIcon(NIM_ADD, icon)) {
    return false;
  }

  Shell_NotifyIcon(NIM_SETVERSION, icon);
  return true;
}

void discardHiddenWindow(TRCONTEXT *context, int index) {
  Shell_NotifyIcon(NIM_DELETE, &context->icons[index].icon);
  removeHiddenWindow(context, index);
  save(context);
}

bool restoreHiddenWindow(TRCONTEXT *context, int index) {
  HWND window = context->icons[index].window;
  if (!IsWindow(window)) {
    discardHiddenWindow(context, index);
    return false;
  }

  WINDOWPLACEMENT placement = {};
  placement.length = sizeof(WINDOWPLACEMENT);
  if (GetWindowPlacement(window, &placement) && placement.showCmd == SW_SHOWMINIMIZED) {
    ShowWindow(window, SW_RESTORE);
  }
  else {
    ShowWindow(window, SW_SHOW);
  }

  SetForegroundWindow(window);
  Shell_NotifyIcon(NIM_DELETE, &context->icons[index].icon);
  removeHiddenWindow(context, index);
  save(context);
  return true;
}

void restoreSavedWindow(TRCONTEXT *context, const std::string& handleString);

// Saves our hidden windows so they can be restored in case
// of crashing.
void save(const TRCONTEXT *context) {
  DWORD numbytes;
  // Truncate file
  SetFilePointer(saveFile, 0, NULL, FILE_BEGIN);
  SetEndOfFile(saveFile);
  if (!context->iconIndex) {
    return;
  }
  for (int i = 0; i < context->iconIndex; i++)
  {
    if (context->icons[i].window) {
      std::string str;
      str = std::to_string(static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(context->icons[i].window)));
      str += ',';
      const char *handleString = str.c_str();
      WriteFile(saveFile, handleString, strlen(handleString), &numbytes, NULL);
    }

  }

}

// Restores a window
void showWindow(TRCONTEXT *context, LPARAM lParam) {
  const UINT iconId = HIWORD(lParam);
  for (int i = 0; i < context->iconIndex; i++)
  {
    if (context->icons[i].icon.uID == iconId) {
      restoreHiddenWindow(context, i);
      break;
    }
  }
}

// Minimizes the current window to tray.
// Uses currently focused window unless supplied a handle as the argument.
void minimizeToTray(TRCONTEXT *context, UINT_PTR restoreWindow) {
  // Taskbar and desktop windows are restricted from hiding.
  const char restrictWins[][14] = { {"WorkerW"}, {"Shell_TrayWnd"} };

  HWND currWin = 0;
  if (!restoreWindow) {
    currWin = GetForegroundWindow();
  }
  else {
    currWin = reinterpret_cast<HWND>(restoreWindow);
  }

  if (!currWin) {
    return;
  }

  char className[256];
  if (!GetClassName(currWin, className, 256)) {
    return;
  }
  else {
    for (int i = 0; i < sizeof(restrictWins) / sizeof(*restrictWins); i++)
    {
      if (strcmp(restrictWins[i], className) == 0) {
        return;
      }
    }
  }
  if (context->iconIndex == MAXIMUM_WINDOWS) {
    MessageBox(NULL, "Error! Too many hidden windows. Please unhide some.", "Traymond", MB_OK | MB_ICONERROR);
    return;
  }

  UINT iconId = allocateIconId(context);
  if (!iconId) {
    MessageBox(NULL, "Error! Could not allocate a tray icon ID.", "Traymond", MB_OK | MB_ICONERROR);
    return;
  }

  ULONG_PTR icon = GetClassLongPtr(currWin, GCLP_HICONSM);
  if (!icon) {
    icon = SendMessage(currWin, WM_GETICON, 2, NULL);
    if (!icon) {
      return;
    }
  }

  NOTIFYICONDATA nid = {};
  nid.cbSize = sizeof(NOTIFYICONDATA);
  nid.hWnd = context->mainWindow;
  nid.hIcon = (HICON)icon;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uVersion = NOTIFYICON_VERSION_4;
  nid.uID = iconId;
  nid.uCallbackMessage = WM_ICON;
  GetWindowText(currWin, nid.szTip, 128);
  context->icons[context->iconIndex].icon = nid;
  context->icons[context->iconIndex].window = currWin;
  if (!addNotifyIcon(&context->icons[context->iconIndex].icon)) {
    if (!restoreWindow) {
      context->icons[context->iconIndex] = {};
      MessageBox(NULL, "Error! Could not add the tray icon.", "Traymond", MB_OK | MB_ICONERROR);
      return;
    }

    SetTimer(context->mainWindow, TRAY_ICON_RETRY_TIMER_ID, TRAY_ICON_RETRY_INTERVAL_MS, NULL);
  }
  context->iconIndex++;
  ShowWindow(currWin, SW_HIDE);
  if (!restoreWindow) {
    save(context);
  }

}

// Adds our own icon to tray
void createTrayIcon(HWND mainWindow, HINSTANCE hInstance, NOTIFYICONDATA* icon) {
  icon->cbSize = sizeof(NOTIFYICONDATA);
  icon->hWnd = mainWindow;
  icon->hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
  icon->uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
  icon->uVersion = NOTIFYICON_VERSION_4;
  icon->uID = MAIN_ICON_ID;
  icon->uCallbackMessage = WM_OURICON;
  strcpy_s(icon->szTip, "Traymond");
}

bool refreshMainTrayIcon(TRCONTEXT *context) {
  if (addNotifyIcon(&context->mainIcon)) {
    context->mainIconVisible = true;
    KillTimer(context->mainWindow, TRAY_ICON_RETRY_TIMER_ID);
    return true;
  }

  context->mainIconVisible = false;
  SetTimer(context->mainWindow, TRAY_ICON_RETRY_TIMER_ID, TRAY_ICON_RETRY_INTERVAL_MS, NULL);
  return false;
}

void refreshAllTrayIcons(TRCONTEXT *context) {
  if (!refreshMainTrayIcon(context)) {
    return;
  }

  for (int i = 0; i < context->iconIndex; i++)
  {
    if (context->icons[i].window && IsWindow(context->icons[i].window)) {
      addNotifyIcon(&context->icons[i].icon);
    }
  }
}

// Creates our tray icon menu
void createTrayMenu(HMENU* trayMenu) {
  *trayMenu = CreatePopupMenu();

  MENUITEMINFO showAllMenuItem = {};
  MENUITEMINFO exitMenuItem = {};

  exitMenuItem.cbSize = sizeof(MENUITEMINFO);
  exitMenuItem.fMask = MIIM_STRING | MIIM_ID;
  exitMenuItem.fType = MFT_STRING;
  exitMenuItem.dwTypeData = "Exit";
  exitMenuItem.cch = 5;
  exitMenuItem.wID = EXIT_ID;

  showAllMenuItem.cbSize = sizeof(MENUITEMINFO);
  showAllMenuItem.fMask = MIIM_STRING | MIIM_ID;
  showAllMenuItem.fType = MFT_STRING;
  showAllMenuItem.dwTypeData = "Restore all windows";
  showAllMenuItem.cch = 20;
  showAllMenuItem.wID = SHOW_ALL_ID;

  InsertMenuItem(*trayMenu, 0, FALSE, &showAllMenuItem);
  InsertMenuItem(*trayMenu, 0, FALSE, &exitMenuItem);
}
// Shows all hidden windows;
void showAllWindows(TRCONTEXT *context) {
  for (int i = 0; i < context->iconIndex; i++)
  {
    ShowWindow(context->icons[i].window, SW_SHOW);
    Shell_NotifyIcon(NIM_DELETE, &context->icons[i].icon);
  }
  ZeroMemory(context->icons, sizeof(context->icons));
  context->iconIndex = 0;
  save(context);
}

void restoreSavedWindow(TRCONTEXT *context, const std::string& handleString) {
  if (handleString.empty()) {
    return;
  }

  char *end = NULL;
  unsigned long long rawHandle = std::strtoull(handleString.c_str(), &end, 10);
  if (end == handleString.c_str() || *end != '\0') {
    return;
  }

  minimizeToTray(context, static_cast<UINT_PTR>(rawHandle));
}

void exitApp() {
  PostQuitMessage(0);
}

// Creates and reads the save file to restore hidden windows in case of unexpected termination
void startup(TRCONTEXT *context) {
  if ((saveFile = CreateFile("traymond.dat", GENERIC_READ | GENERIC_WRITE, \
    0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
    MessageBox(NULL, "Error! Traymond could not create a save file.", "Traymond", MB_OK | MB_ICONERROR);
    exitApp();
  }
  // Check if we've crashed (i. e. there is a save file) during current uptime and
  // if there are windows to restore, in which case restore them and
  // display a reassuring message.
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    DWORD numbytes;
    DWORD fileSize = GetFileSize(saveFile, NULL);

    if (!fileSize) {
      return;
    };

    FILETIME saveFileWriteTime;
    GetFileTime(saveFile, NULL, NULL, &saveFileWriteTime);
    uint64_t writeTime = ((uint64_t)saveFileWriteTime.dwHighDateTime << 32 | (uint64_t)saveFileWriteTime.dwLowDateTime) / 10000;
    GetSystemTimeAsFileTime(&saveFileWriteTime);
    writeTime = (((uint64_t)saveFileWriteTime.dwHighDateTime << 32 | (uint64_t)saveFileWriteTime.dwLowDateTime) / 10000) - writeTime;

    if (GetTickCount64() < writeTime) {
      return;
    }

    std::vector<char> contents = std::vector<char>(fileSize);
    if (!ReadFile(saveFile, &contents.front(), fileSize, &numbytes, NULL)) {
      return;
    }

    std::string handleString;
    handleString.reserve(32);
    for (DWORD i = 0; i < numbytes; i++)
    {
      if (contents[i] == ',') {
        restoreSavedWindow(context, handleString);
        handleString.clear();
      }
      else {
        handleString += contents[i];
      }
    }
    restoreSavedWindow(context, handleString);

    std::string restore_message = "Traymond had previously been terminated unexpectedly.\n\nRestored " + \
      std::to_string(context->iconIndex) + (context->iconIndex > 1 ? " icons." : " icon.");
    MessageBox(NULL, restore_message.c_str(), "Traymond", MB_OK);
    }
  }

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

  TRCONTEXT* context = reinterpret_cast<TRCONTEXT*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  POINT pt;

  if (context && uMsg == context->taskbarRestartMessage) {
    refreshAllTrayIcons(context);
    return 0;
  }

  switch (uMsg)
  {
  case WM_ICON:
    if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK ||
      LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) {
      showWindow(context, lParam);
    }
    break;
  case WM_OURICON:
    if (LOWORD(lParam) == WM_RBUTTONUP) {
      SetForegroundWindow(hwnd);
      GetCursorPos(&pt);
      TrackPopupMenuEx(context->trayMenu, \
      (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_BOTTOMALIGN, \
        pt.x, pt.y, hwnd, NULL);
    }
    break;
  case WM_COMMAND:
    if (HIWORD(wParam) == 0) {
      switch LOWORD(wParam) {
      case SHOW_ALL_ID:
        showAllWindows(context);
        break;
      case EXIT_ID:
        exitApp();
        break;
      }
    }
    break;
  case WM_TIMER:
    if (wParam == TRAY_ICON_RETRY_TIMER_ID) {
      refreshAllTrayIcons(context);
    }
    break;
  case WM_HOTKEY: // We only have one hotkey, so no need to check the message
    minimizeToTray(context, NULL);
    break;
  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

#pragma warning( push )
#pragma warning( disable : 4100 )
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
#pragma warning( pop )

  TRCONTEXT context = {};

  // Mutex to allow only one instance
  const char szUniqueNamedMutex[] = "traymond_mutex";
  HANDLE mutex = CreateMutex(NULL, TRUE, szUniqueNamedMutex);
  if (GetLastError() == ERROR_ALREADY_EXISTS)
  {
    MessageBox(NULL, "Error! Another instance of Traymond is already running.", "Traymond", MB_OK | MB_ICONERROR);
    return 1;
  }

  BOOL bRet;
  MSG msg;

  const char CLASS_NAME[] = "Traymond";

  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;

  if (!RegisterClass(&wc)) {
    return 1;
  }

  context.taskbarRestartMessage = RegisterWindowMessage(TEXT("TaskbarCreated"));

  context.mainWindow = CreateWindowEx(0, CLASS_NAME, "Traymond", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

  if (!context.mainWindow) {
    return 1;
  }

  // Store our context in main window for retrieval by WindowProc
  SetWindowLongPtr(context.mainWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&context));

  if (!RegisterHotKey(context.mainWindow, 0, MOD_KEY | MOD_NOREPEAT, TRAY_KEY)) {
    MessageBox(NULL, "Error! Could not register the hotkey.", "Traymond", MB_OK | MB_ICONERROR);
    return 1;
  }

  createTrayIcon(context.mainWindow, hInstance, &context.mainIcon);
  createTrayMenu(&context.trayMenu);
  refreshAllTrayIcons(&context);
  startup(&context);

  while ((bRet = GetMessage(&msg, 0, 0, 0)) != 0)
  {
    if (bRet != -1) {
      DispatchMessage(&msg);
    }
  }
  // Clean up on exit;
  showAllWindows(&context);
  KillTimer(context.mainWindow, TRAY_ICON_RETRY_TIMER_ID);
  Shell_NotifyIcon(NIM_DELETE, &context.mainIcon);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  CloseHandle(saveFile);
  DestroyMenu(context.trayMenu);
  DestroyWindow(context.mainWindow);
  DeleteFile("traymond.dat"); // No save file means we have exited gracefully
  UnregisterHotKey(context.mainWindow, 0);
  return msg.wParam;
}
