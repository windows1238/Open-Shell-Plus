// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "resource.h"
#include "StartMenuDLL.h"
#include "StartButton.h"
#include "MenuContainer.h"
#include "SettingsParser.h"
#include "Translations.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "ResourceHelper.h"
#include "LogManager.h"
#include "TouchHelper.h"
#include "IatHookHelper.h"
#include "dllmain.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include <htmlhelp.h>
#include <dbghelp.h>
#include <set>
#include <Thumbcache.h>

#define HOOK_DROPTARGET // define this to replace the IDropTarget of the start button
#define START_TOUCH // touch support for the start button

#ifdef BUILD_SETUP
#ifndef HOOK_DROPTARGET
#define HOOK_DROPTARGET // make sure it is defined in Setup
#endif
#endif

const int MAIN_TASK_BAR=0;
typedef std::map<size_t,TaskbarInfo> id_taskbar_map;
id_taskbar_map g_TaskbarInfos;
static int g_LastTaskbar=MAIN_TASK_BAR;
static int g_NextTaskbar=0;
HWND g_TaskBar, g_OwnerWindow;
HWND g_TopWin7Menu, g_AllPrograms, g_ProgramsButton, g_UserPic; // from the Windows menu
HWND g_ProgWin;
HMONITOR g_WSMHMonitor;
static HWND g_WinStartButton;
static UINT g_StartMenuMsg;
static HWND g_Tooltip;
static TOOLINFO g_StartButtonTool;
static bool g_bHotkeyShift;
static int g_HotkeyCSM, g_HotkeyWSM, g_HotkeyShiftID, g_HotkeyCSMID, g_HotkeyWSMID;
static HHOOK g_ProgHook, g_StartHook, g_StartMouseHook, g_AppManagerHook, g_NewWindowHook, g_StartMenuHook;
static bool g_bAllProgramsTimer;
static bool g_bInMenu;
static DWORD g_LastClickTime;
static DWORD g_LastHoverPos;
static bool g_bCrashDump;
static int g_SkipMetroCount;
static DWORD g_StartButtonOldSizes[12];
const int FIRST_BUTTON_BITMAP=6801;
static HWND g_TopDesktopBar;
static DWORD g_AppManagerThread;
static std::set<HWND> g_EdgeWindows;
static bool g_bTrimHooks;
static DWORD g_TaskbarThreadId;
static HWND g_CurrentTaskList, g_CurrentTaskChevron, g_CurrentRebar, g_CurrentTaskbarPart, g_CurrentTaskbarButton, g_CurrentDesktopButton;
static HBITMAP g_TaskbarTexture;
static SIZE g_TaskbarTextureSize;
static TTaskbarTile g_TaskbarTileH, g_TaskbarTileV;
static RECT g_TaskbarMargins;
int g_CurrentCSMTaskbar=-1, g_CurrentWSMTaskbar=-1;

static void FindWindowsMenu( void );
static void RecreateStartButton( size_t taskbarId );
static bool WindowsMenuOpened( void );

static tSetWindowCompositionAttribute SetWindowCompositionAttribute;

enum
{
	OPEN_NOTHING,
	OPEN_CLASSIC,
	OPEN_WINDOWS,
	OPEN_CUSTOM,
	OPEN_BOTH,
	OPEN_DESKTOP,
	OPEN_CORTANA,
};

// MiniDumpNormal - minimal information
// MiniDumpWithDataSegs - include global variables
// MiniDumpWithFullMemory - include heap
MINIDUMP_TYPE MiniDumpType=MiniDumpNormal;

static DWORD WINAPI SaveCrashDump( void *pExceptionInfo )
{
	HMODULE dbghelp=NULL;
	{
		wchar_t path[_MAX_PATH]=L"%LOCALAPPDATA%";
		DoEnvironmentSubst(path,_countof(path));

		dbghelp=LoadLibrary(L"dbghelp.dll");

		LPCTSTR szResult = NULL;

		typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType,
			CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
			CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
			CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam
			);
		MINIDUMPWRITEDUMP dump=NULL;
		if (dbghelp)
			dump=(MINIDUMPWRITEDUMP)GetProcAddress(dbghelp,"MiniDumpWriteDump");
		if (dump)
		{
			HANDLE file;
			for (int i=1;;i++)
			{
				wchar_t fname[_MAX_PATH];
				Sprintf(fname,_countof(fname),L"%s\\CSM_Crash%d.dmp",path,i);
				file=CreateFile(fname,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
				if (file!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_FILE_EXISTS) break;
			}
			if (file!=INVALID_HANDLE_VALUE)
			{
				_MINIDUMP_EXCEPTION_INFORMATION ExInfo;
				ExInfo.ThreadId = GetCurrentThreadId();
				ExInfo.ExceptionPointers = (_EXCEPTION_POINTERS*)pExceptionInfo;
				ExInfo.ClientPointers = NULL;

				dump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpType,&ExInfo,NULL,NULL);
				CloseHandle(file);
			}
		}
	}
	if (dbghelp) FreeLibrary(dbghelp);
	TerminateProcess(GetCurrentProcess(),10);
	return 0;
}

LONG _stdcall TopLevelFilter( _EXCEPTION_POINTERS *pExceptionInfo )
{
	if (pExceptionInfo->ExceptionRecord->ExceptionCode==EXCEPTION_STACK_OVERFLOW)
	{
		// start a new thread to get a fresh stack (hoping there is enough stack left for CreateThread)
		HANDLE thread=CreateThread(NULL,0,SaveCrashDump,pExceptionInfo,0,NULL);
		WaitForSingleObject(thread,INFINITE);
		CloseHandle(thread);
	}
	else
		SaveCrashDump(pExceptionInfo);
	return EXCEPTION_CONTINUE_SEARCH;
}

void InvalidParameterHandler( const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved )
{
	*(int*)0=0; // force a crash to generate a dump
}

///////////////////////////////////////////////////////////////////////////////

interface ISwitchModeManager: public IUnknown
{
	STDMETHOD(method3)();
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(method7)();
	STDMETHOD(ShowLauncherTipContextMenu)( POINT *pt );
};

interface IImmersiveLauncherThumbnailProvider: public IUnknown
{
	STDMETHOD(GetBitmap)( SIZE size, int scale, int, ISharedBitmap **ppBitmap );
};

interface IImmersiveMonitor: public IUnknown
{
	STDMETHOD(method3)();
	STDMETHOD(method4)();
	STDMETHOD(GetHandle)(HMONITOR*);
};

interface IImmersiveLauncher80: public IUnknown
{
	STDMETHOD(ShowStartView)( int method );
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(method7)();
	STDMETHOD(Dismiss)( int method );
};

interface IImmersiveLauncher81: public IUnknown
{
	STDMETHOD(ShowStartView)( int method, int flags );
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(method7)();
	STDMETHOD(method8)();
	STDMETHOD(method9)();
	STDMETHOD(IsVisible)(BOOL *);
	STDMETHOD(method11)();
	STDMETHOD(method12)();
	STDMETHOD(method13)();
	STDMETHOD(method14)();
	STDMETHOD(method15)();
	STDMETHOD(method16)();
	STDMETHOD(method17)();
	STDMETHOD(ConnectToMonitor)(IUnknown *);
	STDMETHOD(GetMonitor)(IImmersiveMonitor **);
};

interface IImmersiveLauncher10RS: public IUnknown
{
	STDMETHOD(ShowStartView)( int method, int flags );
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(IsVisible)(BOOL *);
	STDMETHOD(method8)();
	STDMETHOD(method9)();
	STDMETHOD(ConnectToMonitor)(IUnknown *);
	STDMETHOD(GetMonitor)(IImmersiveMonitor **);
};

static const GUID SID_SwitchModeManager={0x085920a1,0x28d3,0x44c1,{0x89,0x7d,0x3b,0xe6,0xd0,0x4b,0x2e,0x07}};
static const GUID IID_ISwitchModeManager={0x976c17be,0xe2d5,0x4f36,{0x93,0x4a,0x7e,0x82,0xf7,0x10,0xea,0xe1}};

static const GUID SID_ImmersiveLauncherThumbnailProvider={0x66ce8036,0x400c,0x42f7,{0x99,0x34,0x02,0xf8,0x84,0xfe,0x27,0x4f}};
static const GUID IID_IImmersiveLauncherThumbnailProvider={0x35c01454,0x53f4,0x4818,{0xba,0x8c,0x7a,0xba,0xdc,0x0f,0xfe,0xe6}};

static const GUID SID_ImmersiveLauncher={0x6f86e01c,0xc649,0x4d61,{0xbe,0x23,0xf1,0x32,0x2d,0xde,0xca,0x9d}};
static const GUID IID_IImmersiveLauncher80={0xfd8b3e33,0xa1f7,0x4e9a,{0x80,0xad,0x80,0x02,0xc7,0x46,0xbe,0x37}};
static const GUID IID_IImmersiveLauncher81={0x93f91f5a,0xa4ca,0x4205,{0x9b,0xeb,0xce,0x4d,0x17,0xc7,0x08,0xf9}};
static const GUID IID_IImmersiveLauncher10RS={0xd8d60399,0xa0f1,0xf987,{0x55,0x51,0x32,0x1f,0xd1,0xb4,0x98,0x64}}; // 14257

static const GUID IID_IImmersiveLauncherProvider={0x6d5140c1,0x7436,0x11ce,{0x80,0x34,0x00,0xaa,0x00,0x60,0x09,0xfa}};

static const CLSID CLSID_ImmersiveShell={0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};

static const GUID SID_LauncherTipContextMenu={0xb8c1db5f, 0xcbb3, 0x48bc, {0xaf, 0xd9, 0xce, 0x6b, 0x88, 0x0c, 0x79, 0xed}};

interface ILauncherTipContextMenu: public IUnknown
{
	STDMETHOD(ShowLauncherTipContextMenu)( POINT *pt );
};

interface IImmersiveMonitorService: public IUnknown
{
	STDMETHOD(method3)();
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(GetFromHandle)(HMONITOR, IUnknown **);
	STDMETHOD(method7)();
	STDMETHOD(method8)();
	STDMETHOD(method9)();
	STDMETHOD(method10)();
	STDMETHOD(method11)();
	STDMETHOD(method12)();
	STDMETHOD(method13)();
	STDMETHOD(SetImmersiveMonitor)(IUnknown *);
};

static const GUID SID_IImmersiveMonitorService={0x47094e3a,0x0cf2,0x430f,{0x80,0x6f,0xcf,0x9e,0x4f,0x0f,0x12,0xdd}};
static const GUID IID_IImmersiveMonitorService={0x4d4c1e64,0xe410,0x4faa,{0xba,0xfa,0x59,0xca,0x06,0x9b,0xfe,0xc2}};


struct StartScreenThumbInfo
{
	SIZE size;
	HBITMAP bitmap;
	HANDLE event;
};

static bool CreateImmersiveShell( CComPtr<IUnknown> &ptr )
{
	if (GetWinVersion()<WIN_VER_WIN8)
		return false;
	ptr.CoCreateInstance(CLSID_ImmersiveShell);
	return ptr.p!=NULL;
}

///////////////////////////////////////////////////////////////////////////////

// COwnerWindow - a special window used as owner for some UI elements, like the ones created by IContextMenu::InvokeCommand.
// A menu window cannot be used because it may disappear immediately after InvokeCommand. Some UI elements, like the UAC-related
// stuff can be created long after InvokeCommand returns and the menu may be deleted by then.
class COwnerWindow: public CWindowImpl<COwnerWindow>
{
public:
	DECLARE_WND_CLASS_EX(L"OpenShell.COwnerWindow",0,COLOR_MENU)

	// message handlers
	BEGIN_MSG_MAP( COwnerWindow )
		MESSAGE_HANDLER( WM_ACTIVATE, OnActivate )
		MESSAGE_HANDLER( WM_SYSCOLORCHANGE, OnColorChange )
		MESSAGE_HANDLER( WM_SETTINGCHANGE, OnSettingChange )
		MESSAGE_HANDLER( WM_DISPLAYCHANGE, OnDisplayChange )
	END_MSG_MAP()

protected:
	LRESULT OnActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (LOWORD(wParam)!=WA_INACTIVE)
			return 0;

		if (CMenuContainer::s_bPreventClosing)
			return 0;

		// check if another menu window is being activated
		// if not, close all menus
		for (std::vector<CMenuContainer*>::const_iterator it=CMenuContainer::s_Menus.begin();it!=CMenuContainer::s_Menus.end();++it)
			if ((*it)->m_hWnd==(HWND)lParam)
				return 0;

		for (std::vector<CMenuContainer*>::reverse_iterator it=CMenuContainer::s_Menus.rbegin();it!=CMenuContainer::s_Menus.rend();++it)
			if (!(*it)->m_bDestroyed)
				(*it)->PostMessage(WM_CLOSE);

		return 0;
	}


	LRESULT OnColorChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		CMenuContainer::s_Skin.Hash=0;
		return 0;
	}


	LRESULT OnSettingChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (wParam==SPI_SETWORKAREA)
		{
			if (!CMenuContainer::s_Menus.empty())
				CMenuContainer::s_Menus[0]->NotifyDisplayChange();
		}
		return 0;
	}


	LRESULT OnDisplayChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (!CMenuContainer::s_Menus.empty())
			CMenuContainer::s_Menus[0]->NotifyDisplayChange();
		return 0;
	}
};

static COwnerWindow g_Owner;

///////////////////////////////////////////////////////////////////////////////

bool TaskbarInfo::HasPart( HWND part ) const
{
	for (std::vector<HWND>::const_iterator it=taskbarParts.begin();it!=taskbarParts.end();++it)
		if (*it==part)
			return true;
	return false;
}

static const TaskbarInfo *GetDefaultTaskbarInfo( void )
{
	if (GetSettingBool(L"AllTaskbars"))
	{
		HMONITOR monitor=MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONEAREST);
		for (std::map<size_t,TaskbarInfo>::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		{
			MONITORINFO info;
			HMONITOR monitor2=NULL;
			if (GetTaskbarPosition(it->second.taskBar,&info,&monitor2,NULL)!=0xFFFFFFFF && monitor2==monitor)
				return &it->second;
		}
		id_taskbar_map::const_iterator it=g_TaskbarInfos.find(g_LastTaskbar);
		if (it!=g_TaskbarInfos.end())
			return &it->second;
	}
	return &g_TaskbarInfos.begin()->second;
}

TaskbarInfo *GetTaskbarInfo( size_t taskbarId )
{
	std::map<size_t,TaskbarInfo>::iterator it=g_TaskbarInfos.find(taskbarId);
	return (it==g_TaskbarInfos.end())?NULL:&it->second;
}

static TaskbarInfo *FindTaskBarInfoButton( HWND button )
{
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		if (it->second.startButton==button || it->second.oldButton==button)
			return &it->second;
	return NULL;
}

static TaskbarInfo *FindTaskBarInfoBar( HWND bar )
{
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		if (it->second.taskBar==bar)
			return &it->second;
	return NULL;
}

static LRESULT CALLBACK HookProgManThread( int code, WPARAM wParam, LPARAM lParam );
static LRESULT CALLBACK HookDesktopThread( int code, WPARAM wParam, LPARAM lParam );
static LRESULT CALLBACK HookDesktopThreadMouse(int code, WPARAM wParam, LPARAM lParam);

static BOOL CALLBACK FindTooltipEnum( HWND hwnd, LPARAM lParam )
{
	// look for tooltip control in the current thread that has a tool for g_TaskBar+g_StartButton
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,TOOLTIPS_CLASS)!=0) return TRUE;
	TOOLINFO info={sizeof(info),0,g_TaskBar,(UINT_PTR)g_WinStartButton};
	if (SendMessage(hwnd,TTM_GETTOOLINFO,0,(LPARAM)&info))
	{
		g_Tooltip=hwnd;
		return FALSE;
	}
	return TRUE;
}

static BOOL CALLBACK FindStartButtonEnum( HWND hwnd, LPARAM lParam )
{
	// look for top-level window in the current thread with class "button"
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,L"button")!=0) return TRUE;
	g_WinStartButton=hwnd;
	return FALSE;
}

static BOOL CALLBACK FindTaskBarEnum( HWND hwnd, LPARAM lParam )
{
	// look for top-level window with class "Shell_TrayWnd" and process ID=lParam
	DWORD process;
	GetWindowThreadProcessId(hwnd,&process);
	if (process!=lParam) return TRUE;
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,L"Shell_TrayWnd")!=0) return TRUE;
	g_TaskBar=hwnd;
	return FALSE;
}

// Find the taskbar window for the given process
STARTMENUAPI HWND FindTaskBar( DWORD process )
{
	g_WinStartButton=NULL;
	g_TaskBar=NULL;
	g_Tooltip=NULL;
	// find the taskbar
	EnumWindows(FindTaskBarEnum,process);
	if (!g_TaskBar)
		g_TaskBar=FindWindowEx(GetDesktopWindow(),NULL,L"Shell_TrayWnd",NULL);
	if (g_TaskBar)
	{
		// find start button
		if (GetWinVersion()==WIN_VER_WIN7)
			EnumThreadWindows(GetWindowThreadProcessId(g_TaskBar,NULL),FindStartButtonEnum,NULL);
		if (GetWindowThreadProcessId(g_TaskBar,NULL)==GetCurrentThreadId())
		{
			// find tooltip
			if (g_WinStartButton)
			{
				EnumThreadWindows(GetWindowThreadProcessId(g_TaskBar,NULL),FindTooltipEnum,NULL);
				if (g_Tooltip)
				{
					g_StartButtonTool.cbSize=sizeof(g_StartButtonTool);
					g_StartButtonTool.hwnd=g_TaskBar;
					g_StartButtonTool.uId=(UINT_PTR)g_WinStartButton;
					SendMessage(g_Tooltip,TTM_GETTOOLINFO,0,(LPARAM)&g_StartButtonTool);
				}
			}
			g_OwnerWindow=g_Owner.Create(NULL,0,0,WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST);
		}
	}
	return g_TaskBar;
}

#ifdef HOOK_DROPTARGET
class CStartMenuTarget: public IDropTarget
{
public:
	CStartMenuTarget( int taskbarId ) { m_RefCount=1; m_TaskbarId=taskbarId; }
	// IUnknown
	virtual STDMETHODIMP QueryInterface( REFIID riid, void **ppvObject )
	{
		*ppvObject=NULL;
		if (IID_IUnknown==riid || IID_IDropTarget==riid)
		{
			AddRef();
			*ppvObject=(IDropTarget*)this;
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef( void ) 
	{ 
		return InterlockedIncrement(&m_RefCount);
	}

	virtual ULONG STDMETHODCALLTYPE Release( void )
	{
		long nTemp=InterlockedDecrement(&m_RefCount);
		if (!nTemp) delete this;
		return nTemp;
	}

	// IDropTarget
	virtual HRESULT STDMETHODCALLTYPE DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
	{
		FORMATETC format1={(CLIPFORMAT)RegisterClipboardFormat(CFSTR_SHELLIDLIST),NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
		FORMATETC format2={(CLIPFORMAT)RegisterClipboardFormat(CFSTR_INETURL),NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
		if (pDataObj->QueryGetData(&format1)==S_OK || pDataObj->QueryGetData(&format2)==S_OK)
		{
			PostMessage(g_TaskBar,g_StartMenuMsg,(grfKeyState&MK_SHIFT)?MSG_SHIFTDRAG:MSG_DRAG,m_TaskbarId);
		}
		*pdwEffect=DROPEFFECT_NONE;
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) { return *pdwEffect=DROPEFFECT_NONE; return S_OK; }
	virtual HRESULT STDMETHODCALLTYPE DragLeave( void ) { return S_OK; }
	virtual HRESULT STDMETHODCALLTYPE Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) { return *pdwEffect=DROPEFFECT_NONE; return S_OK; }

private:
	LONG m_RefCount;
	int m_TaskbarId;
};

#endif

static CComPtr<IDropTarget> g_pOriginalTarget;

static void FindTaskBar( void )
{
	if (!g_TaskBar)
	{
		g_StartMenuMsg=RegisterWindowMessage(L"OpenShellMenu.StartMenuMsg");
		FindTaskBar(GetCurrentProcessId());
		if (g_TaskBar)
		{
			g_HotkeyShiftID=GlobalAddAtom(L"OpenShellMenu.HotkeyShift");
			g_HotkeyCSMID=GlobalAddAtom(L"OpenShellMenu.HotkeyCSM");
			g_HotkeyWSMID=GlobalAddAtom(L"OpenShellMenu.HotkeyWSM");
			EnableHotkeys(HOTKEYS_NORMAL);
			srand(GetTickCount());
		}
		if (!g_TaskBar) g_TaskBar=(HWND)1;
	}
}

void EnableStartTooltip( bool bEnable )
{
	if (g_Tooltip)
	{
		SendMessage(g_Tooltip,TTM_POP,0,0);
		if (bEnable)
			SendMessage(g_Tooltip,TTM_UPDATETIPTEXT,0,(LPARAM)&g_StartButtonTool);
		else
		{
			TOOLINFO info=g_StartButtonTool;
			info.lpszText=(LPWSTR)L"";
			SendMessage(g_Tooltip,TTM_UPDATETIPTEXT,0,(LPARAM)&info);
		}
	}
}

// Restore the original drop target
static void UnhookDropTarget( void )
{
	if (g_pOriginalTarget)
	{
		RevokeDragDrop(g_WinStartButton);
		if (g_pOriginalTarget)
			RegisterDragDrop(g_WinStartButton,g_pOriginalTarget);
		g_pOriginalTarget=NULL;
	}
}

// Toggle the start menu. bKeyboard - set to true to show the keyboard cues
STARTMENUAPI HWND ToggleStartMenu( int taskbarId, bool bKeyboard )
{
	if (taskbarId==-1)
	{
		if (g_TaskbarInfos.find(-1)==g_TaskbarInfos.end())
		{
			g_TaskbarInfos[-1].taskBar=g_TaskBar;
		}
	}
	g_LastTaskbar=taskbarId;
	return CMenuContainer::ToggleStartMenu(taskbarId,bKeyboard,false);
}

UINT GetTaskbarPosition( HWND taskBar, MONITORINFO *pInfo, HMONITOR *pMonitor, RECT *pRc )
{
	if (!IsWindow(taskBar))
		return 0xFFFFFFFF;
	if (taskBar==g_TaskBar)
	{
		APPBARDATA appbar={sizeof(appbar),taskBar};
		SHAppBarMessage(ABM_GETTASKBARPOS,&appbar);
		if (pRc)
		{
			*pRc=appbar.rc;
			RECT rc;
			GetWindowRect(taskBar,&rc);
			if (appbar.uEdge==ABE_LEFT || appbar.uEdge==ABE_RIGHT)
			{
				if (pRc->top<rc.top) pRc->top=rc.top;
				if (pRc->bottom>rc.bottom) pRc->bottom=rc.bottom;
			}
			else if (appbar.uEdge==ABE_TOP || appbar.uEdge==ABE_BOTTOM)
			{
				if (pRc->left<rc.left) pRc->left=rc.left;
				if (pRc->right>rc.right) pRc->right=rc.right;
			}
		}
		HMONITOR monitor=MonitorFromRect(&appbar.rc,MONITOR_DEFAULTTONEAREST);
		if (pMonitor) *pMonitor=monitor;
		if (pInfo)
		{
			pInfo->cbSize=sizeof(MONITORINFO);
			GetMonitorInfo(monitor,pInfo);
		}
		return appbar.uEdge;
	}
	RECT rc;
	if (GetWindowRgnBox(taskBar,&rc)!=ERROR)
		MapWindowPoints(taskBar,NULL,(POINT*)&rc,2);
	else
		GetWindowRect(taskBar,&rc);
	MONITORINFO info={sizeof(info)};
	HMONITOR monitor=MonitorFromRect(&rc,MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(monitor,&info);
	if (pMonitor) *pMonitor=monitor;
	int dx=rc.left+rc.right-info.rcWork.left-info.rcWork.right;
	int dy=rc.top+rc.bottom-info.rcWork.top-info.rcWork.bottom;
	if (pInfo) *pInfo=info;
	bool bAutoHide=false;
	if (pRc)
	{
		GetWindowRect(taskBar,pRc);
		APPBARDATA appbar={sizeof(appbar)};
		bAutoHide=(SHAppBarMessage(ABM_GETSTATE,&appbar)&ABS_AUTOHIDE)!=0;
	}
	if (dx<-abs(dy))
	{
		if (bAutoHide && pRc->left<info.rcWork.left)
			OffsetRect(pRc,info.rcWork.left-pRc->left,0);
		 return ABE_LEFT;
	}
	if (dx>abs(dy))
	{
		if (bAutoHide && pRc->right>info.rcWork.right)
			OffsetRect(pRc,info.rcWork.right-pRc->right,0);
		return ABE_RIGHT;
	}
	if (dy<-abs(dx))
	{
		if (bAutoHide && pRc->top<info.rcWork.top)
			OffsetRect(pRc,0,info.rcWork.top-pRc->top);
		return ABE_TOP;
	}
	if (bAutoHide && pRc->bottom>info.rcWork.bottom)
		OffsetRect(pRc,0,info.rcWork.bottom-pRc->bottom);
	return ABE_BOTTOM;
}

// Returns true if the mouse is on the taskbar portion of the start button
bool PointAroundStartButton( size_t taskbarId, const CPoint &pt )
{
	const TaskbarInfo *taskBar=GetTaskbarInfo(taskbarId);
	if (!taskBar || !(taskBar->startButton || taskBar->oldButton)) return false;
	CRect rc;
	GetWindowRect(taskBar->taskBar,&rc);
	if (!PtInRect(&rc,pt))
		return false;

	bool rtl=GetWindowLongPtr(taskBar->taskBar,GWL_EXSTYLE)&WS_EX_LAYOUTRTL;

	CRect rcStart;
	if (taskBar->startButton)
		GetWindowRect(taskBar->startButton,&rcStart);

	CRect rcOld;
	if (taskBar->oldButton)
	{
		GetWindowRect(taskBar->oldButton,&rcOld);

		if (IsWin11())
		{
			// on Win11 the Start button rectangle is a bit smaller that actual XAML active area
			// lets make it a bit wider to avoid accidental original Start menu triggers
			const int adjust=ScaleForDpi(taskBar->taskBar,1);
			if (rtl)
				rcOld.left-=adjust;
			else
				rcOld.right+=adjust;
		}
	}

	rc.UnionRect(&rcStart,&rcOld);

	// check if the point is inside the start button rect
	UINT uEdge=GetTaskbarPosition(taskBar->taskBar,NULL,NULL,NULL);
	if (uEdge==ABE_LEFT || uEdge==ABE_RIGHT)
		return pt.y>=rc.top && pt.y<rc.bottom;
	else if (rtl)
		return pt.x>rc.left && pt.x<=rc.right;
	else
		return pt.x>=rc.left && pt.x<rc.right;
}

// declare few interfaces so we don't need the Win8 SDK
#ifndef __IAppVisibility_INTERFACE_DEFINED__
typedef enum MONITOR_APP_VISIBILITY
{
	MAV_UNKNOWN	= 0,
	MAV_NO_APP_VISIBLE	= 1,
	MAV_APP_VISIBLE	= 2
} MONITOR_APP_VISIBILITY;

MIDL_INTERFACE("6584CE6B-7D82-49C2-89C9-C6BC02BA8C38")
IAppVisibilityEvents : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE AppVisibilityOnMonitorChanged( HMONITOR hMonitor, MONITOR_APP_VISIBILITY previousMode, MONITOR_APP_VISIBILITY currentMode ) = 0;
	virtual HRESULT STDMETHODCALLTYPE LauncherVisibilityChange( BOOL currentVisibleState ) = 0;
};

MIDL_INTERFACE("2246EA2D-CAEA-4444-A3C4-6DE827E44313")
IAppVisibility : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE GetAppVisibilityOnMonitor( HMONITOR hMonitor, MONITOR_APP_VISIBILITY *pMode ) = 0;
	virtual HRESULT STDMETHODCALLTYPE IsLauncherVisible( BOOL *pfVisible ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Advise( IAppVisibilityEvents *pCallback, DWORD *pdwCookie ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unadvise( DWORD dwCookie ) = 0;
};

#endif

void ResetHotCorners( void )
{
	for (std::set<HWND>::const_iterator it=g_EdgeWindows.begin();it!=g_EdgeWindows.end();++it)
		ShowWindow(*it,SW_SHOW);
	g_EdgeWindows.clear();
}

void RedrawTaskbars( void )
{
	for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		InvalidateRect(it->second.taskBar,NULL,TRUE);
}

static CComPtr<IAppVisibility> g_pAppVisibility;
static DWORD g_AppVisibilityMonitorCookie;

class CMonitorModeEvents: public IAppVisibilityEvents
{
public:
	CMonitorModeEvents( void ) { m_RefCount=1; }
	// IUnknown
	virtual STDMETHODIMP QueryInterface( REFIID riid, void **ppvObject )
	{
		*ppvObject=NULL;
		if (IID_IUnknown==riid || __uuidof(IAppVisibilityEvents)==riid)
		{
			AddRef();
			*ppvObject=(IDropTarget*)this;
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef( void )
	{
		return InterlockedIncrement(&m_RefCount);
	}

	virtual ULONG STDMETHODCALLTYPE Release( void )
	{
		long nTemp=InterlockedDecrement(&m_RefCount);
		if (!nTemp) delete this;
		return nTemp;
	}

	// IAppVisibilityEvents
	virtual HRESULT STDMETHODCALLTYPE AppVisibilityOnMonitorChanged( HMONITOR hMonitor, MONITOR_APP_VISIBILITY previousMode, MONITOR_APP_VISIBILITY currentMode )
	{
		if (GetWinVersion()<WIN_VER_WIN10)
		{
			ResetHotCorners();
			if (IsWin81Update1() && GetSettingBool(L"CustomTaskbar"))
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_REDRAWTASKBAR,0);
		}
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE LauncherVisibilityChange( BOOL currentVisibleState )
	{
		CComPtr<IUnknown> pImmersiveShell;
		if (GetWinVersion()>=WIN_VER_WIN10 && CreateImmersiveShell(pImmersiveShell))
		{
			int taskbarId=-1;
			if (currentVisibleState)
			{
				taskbarId=MAIN_TASK_BAR;
				CComPtr<IImmersiveMonitor> pMonitor;
				{
					CComPtr<IImmersiveLauncher81> pLauncher;
					IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher81,(void**)&pLauncher);
					if (pLauncher)
						pLauncher->GetMonitor(&pMonitor);
				}
				if (!pMonitor)
				{
					CComPtr<IImmersiveLauncher10RS> pLauncher;
					IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher10RS,(void**)&pLauncher);
					if (pLauncher)
						pLauncher->GetMonitor(&pMonitor);
				}
				if (pMonitor)
				{
					HMONITOR monitor;
					if (SUCCEEDED(pMonitor->GetHandle(&monitor)))
					{
						for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
						{
							if (monitor==MonitorFromWindow(it->second.taskBar,MONITOR_DEFAULTTONULL))
							{
								taskbarId=it->second.taskbarId;
								break;
							}
						}
					}
				}
			}
			if (g_CurrentWSMTaskbar!=taskbarId)
			{
				if (g_CurrentWSMTaskbar!=-1 && g_CurrentWSMTaskbar!=g_CurrentCSMTaskbar)
					PressStartButton(g_CurrentWSMTaskbar,false);
				g_CurrentWSMTaskbar=taskbarId;
				if (g_CurrentWSMTaskbar!=-1)
					PressStartButton(g_CurrentWSMTaskbar,true);
			}
		}
		else
		{
			ResetHotCorners();
			if (IsWin81Update1() && GetSettingBool(L"CustomTaskbar"))
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_REDRAWTASKBAR,0);
		}
		return S_OK;
	}

private:
	LONG m_RefCount;
};

static const CLSID CLSID_MetroMode={0x7E5FE3D9,0x985F,0x4908,{0x91, 0xF9, 0xEE, 0x19, 0xF9, 0xFD, 0x15, 0x14}};

BOOL CALLBACK AppVisibleProc( HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData )
{
	bool *pData=(bool*)dwData;
	MONITOR_APP_VISIBILITY mode;
	if (SUCCEEDED(g_pAppVisibility->GetAppVisibilityOnMonitor(hMonitor,&mode)) && mode==MAV_APP_VISIBLE)
		*pData=true;
	return !*pData;
}

enum TMetroMode
{
	METRO_NONE,
	METRO_LAUNCHER,
	METRO_APP,
};

static TMetroMode GetMetroMode( HMONITOR hMonitor )
{
	if (!g_pAppVisibility) return METRO_NONE;

	BOOL bLauncher;
	if (SUCCEEDED(g_pAppVisibility->IsLauncherVisible(&bLauncher)) && bLauncher)
	{
		if (!hMonitor) return METRO_LAUNCHER;
		HWND launcher=FindWindow(L"ImmersiveLauncher",NULL);
		if (launcher && hMonitor==MonitorFromWindow(launcher,MONITOR_DEFAULTTONULL))
				return METRO_LAUNCHER;
	}

	if (hMonitor)
	{
		MONITOR_APP_VISIBILITY mode;
		if (SUCCEEDED(g_pAppVisibility->GetAppVisibilityOnMonitor(hMonitor,&mode)) && mode==MAV_APP_VISIBLE)
			return METRO_APP;
	}
	else
	{
		bool bAppVisible=false;
		EnumDisplayMonitors(NULL,NULL,AppVisibleProc,(LPARAM)&bAppVisible);
		if (bAppVisible) return METRO_APP;
	}
	return METRO_NONE;
}

static bool GetWin10TabletMode( void )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
	{
		DWORD val;
		return regKey.QueryDWORDValue(L"TabletMode",val)==ERROR_SUCCESS && val;
	}
	return false;
}

static LRESULT CALLBACK HookAppManager( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HC_ACTION && wParam)
	{
		MSG *msg=(MSG*)lParam;
		if (msg->message==g_StartMenuMsg && (msg->wParam==MSG_WINXMENU || msg->wParam==MSG_METROTHUMBNAIL || msg->wParam==MSG_SHIFTWIN))
		{
			HWND hwnd=FindWindow(L"ModeInputWnd",NULL);
			if (hwnd)
			{
				DWORD process;
				GetWindowThreadProcessId(hwnd,&process);
				if (process==GetCurrentProcessId())
				{
					IObjectWithSite *pObject=(IObjectWithSite*)GetWindowLongPtr(hwnd,0);
					if (pObject)
					{
						CComPtr<IUnknown> pSite;
						pObject->GetSite(IID_IUnknown,(void**)&pSite);
						if (pSite)
						{
							if (msg->wParam==MSG_WINXMENU)
							{
								CPoint pt(msg->lParam);
								CComPtr<ISwitchModeManager> pSwitchModeManager;
								IUnknown_QueryService(pSite,SID_SwitchModeManager,IID_ISwitchModeManager,(void**)&pSwitchModeManager);
								if (pSwitchModeManager)
								{
									pSwitchModeManager->ShowLauncherTipContextMenu(&pt);
									// set the current immersive monitor AFTER the menu returns (this way Search is shown in the correct monitor)
									CComPtr<IImmersiveMonitorService> pMonitorService;
									IUnknown_QueryService(pSite,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
									if (pMonitorService)
									{
										HMONITOR monitor=MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST);
										if (GetWinVersion()==WIN_VER_WIN8)
										{
											CComPtr<IUnknown> pMonitor;
											pMonitorService->GetFromHandle(monitor,&pMonitor);
											if (pMonitor)
												pMonitorService->SetImmersiveMonitor(pMonitor);
										}
										else if (GetWinVersion()>WIN_VER_WIN8)
										{
											// doesn't seem to be doing anything on 8.1, but do it just in case
											CComPtr<IUnknown> pMonitor;
											pMonitorService->GetFromHandle(monitor,&pMonitor);
											if (pMonitor)
											{
												CComPtr<IImmersiveLauncher81> pLauncher;
												IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncher81,(void**)&pLauncher);
												if (pLauncher)
													pLauncher->ConnectToMonitor(pMonitor);
											}
										}
									}
								}
							}
							if (msg->wParam==MSG_METROTHUMBNAIL)
							{
								StartScreenThumbInfo &info=*(StartScreenThumbInfo*)msg->lParam;
								CComPtr<IUnknown> pLauncher;
								IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncherProvider,(void**)&pLauncher);
								if (pLauncher)
								{
									CComPtr<IImmersiveLauncherThumbnailProvider> pProvider;
									IUnknown_QueryService(pLauncher,SID_ImmersiveLauncherThumbnailProvider,IID_IImmersiveLauncherThumbnailProvider,(void**)&pProvider);
									if (pProvider)
									{
										CComPtr<ISharedBitmap> pBitmap;
										if (SUCCEEDED(pProvider->GetBitmap(info.size,100,1,&pBitmap)) && pBitmap)
										{
											pBitmap->Detach(&info.bitmap);
										}
									}
								}
							}
							if (msg->wParam==MSG_SHIFTWIN)
							{
								if (GetWinVersion()==WIN_VER_WIN8)
								{
									HMONITOR monitor=(HMONITOR)msg->lParam;
									if (monitor)
									{
										CComPtr<IImmersiveMonitorService> pMonitorService;
										IUnknown_QueryService(pSite,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
										if (pMonitorService)
										{
											CComPtr<IUnknown> pMonitor;
											pMonitorService->GetFromHandle(monitor,&pMonitor);
											if (pMonitor)
												pMonitorService->SetImmersiveMonitor(pMonitor);
										}
									}
									CComPtr<IImmersiveLauncher80> pLauncher;
									IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncher80,(void**)&pLauncher);
									if (pLauncher)
										pLauncher->ShowStartView(5);
								}
							}
						}
					}
				}
			}
			if (msg->wParam==MSG_METROTHUMBNAIL)
			{
				// set the event no matter if successful
				StartScreenThumbInfo &info=*(StartScreenThumbInfo*)msg->lParam;
				SetEvent(info.event);
			}
		}
		int corner;
		if ((msg->message==WM_MOUSEMOVE || msg->message==WM_LBUTTONDOWN) && (corner=GetSettingInt(L"DisableHotCorner"))>0)
		{
			{
				// ignore the mouse messages if there is a menu
				GUITHREADINFO info={sizeof(info)};
				if (GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags&GUI_INMENUMODE))
					return CallNextHookEx(NULL,code,wParam,lParam);
			}
			CPoint pt(GetMessagePos());
			HMONITOR monitor=MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST);
			if (GetMetroMode(monitor)!=METRO_NONE)
			{
				if (!IsWin81Update1())
					return CallNextHookEx(NULL,code,wParam,lParam);
				typedef BOOL (WINAPI *tGetWindowBand)(HWND,DWORD*);
				static tGetWindowBand GetWindowBand=(tGetWindowBand)GetProcAddress(GetModuleHandle(L"user32.dll"),"GetWindowBand");
				for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
				{
					DWORD band;
					if (!GetWindowBand || !GetWindowBand(it->second.taskBar,&band) || band==1)
						continue;
					UINT uEdge=GetTaskbarPosition(it->second.taskBar,NULL,NULL,NULL);
					if (uEdge!=ABE_BOTTOM)
						continue;

					// check if the mouse is over the taskbar
					RECT taskRect;
					GetWindowRect(it->second.taskBar,&taskRect);
					if (PtInRect(&taskRect,pt))
					{
						POINT pt2=pt;
						ScreenToClient(it->second.taskBar,&pt2);
						if (pt2.x<32)
						{
							if (msg->message==WM_LBUTTONDOWN)
							{
								// forward the mouse click to the taskbar
								PostMessage(it->second.taskBar,WM_NCLBUTTONDOWN,MK_LBUTTON,MAKELONG(pt.x,pt.y));
								msg->message=WM_NULL;
							}
							wchar_t className[256]={0};
							GetClassName(msg->hwnd,className,_countof(className));
							if (wcscmp(className,L"ImmersiveSwitchList")==0)
							{
								// suppress the opening of the ImmersiveSwitchList
								msg->message=WM_NULL;
								ShowWindow(msg->hwnd,SW_HIDE); // hide the popup
							}
							if (wcscmp(className,L"EdgeUiInputWndClass")==0)
							{
								// suppress the hot corners
								msg->message=WM_NULL;
							}
						}
						break;
					}
				}
				return CallNextHookEx(NULL,code,wParam,lParam);
			}
			if (corner==1)
			{
				for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
				{
					UINT uEdge=GetTaskbarPosition(it->second.taskBar,NULL,NULL,NULL);
					if (uEdge==ABE_BOTTOM)
					{
						// check if the mouse is over the taskbar
						RECT taskRect;
						GetWindowRect(it->second.taskBar,&taskRect);
						if (PtInRect(&taskRect,pt))
						{
							POINT pt2=pt;
							ScreenToClient(it->second.taskBar,&pt2);
							if (pt2.x<32)
							{
								corner=2;
								if (msg->message==WM_LBUTTONDOWN)
								{
									// forward the mouse click to the taskbar
									PostMessage(it->second.taskBar,WM_NCLBUTTONDOWN,MK_LBUTTON,MAKELONG(pt.x,pt.y));
									msg->message=WM_NULL;
								}
								wchar_t className[256]={0};
								GetClassName(msg->hwnd,className,_countof(className));
								if (wcscmp(className,L"ImmersiveSwitchList")==0)
								{
									// suppress the opening of the ImmersiveSwitchList
									msg->message=WM_NULL;
									ShowWindow(msg->hwnd,SW_HIDE); // hide the popup
								}
							}
							break;
						}
					}
				}
			}
			if (corner==2)
			{
				wchar_t className[256]={0};
				GetClassName(msg->hwnd,className,_countof(className));
				if (wcscmp(className,L"EdgeUiInputWndClass")==0)
				{
					// suppress the hot corners
					msg->message=WM_NULL;
					ShowWindow(msg->hwnd,SW_HIDE);
					g_EdgeWindows.insert(msg->hwnd);
				}
			}
		}
	}
	return CallNextHookEx(NULL,code,wParam,lParam);
}

static LRESULT CALLBACK HookNewWindow( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HCBT_CREATEWND)
	{
		CBT_CREATEWND *pCreate=(CBT_CREATEWND*)lParam;
		if (pCreate->lpcs->lpszClass>(LPTSTR)0xFFFF && (_wcsicmp(pCreate->lpcs->lpszClass,L"Shell_SecondaryTrayWnd")==0 ||
			_wcsicmp(pCreate->lpcs->lpszClass,L"ToolbarWindow32")==0 || _wcsicmp(pCreate->lpcs->lpszClass,L"TrayClockWClass")==0 || _wcsicmp(pCreate->lpcs->lpszClass,L"ClockButton")==0))
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_NEWTASKBAR,wParam);
	}
	return CallNextHookEx(NULL,code,wParam,lParam);
}

// Set the hotkeys and controls for the start menu
void EnableHotkeys( THotkeys enable )
{
	if (g_bTrimHooks) return;
	if (!g_TaskBar)
		return;
	if (GetWindowThreadProcessId(g_TaskBar,NULL)!=GetCurrentThreadId())
	{
		PostMessage(g_TaskBar,g_StartMenuMsg,MSG_HOTKEYS,enable);
		return;
	}

	// must be executed in the same thread as the start button (otherwise RegisterHotKey doesn't work). also prevents race conditions
	bool bHook=(enable==HOTKEYS_SETTINGS || (enable==HOTKEYS_NORMAL && GetSettingInt(L"ShiftWin")!=0));
	if (bHook)
	{
		RegisterHotKey(g_TaskBar,g_HotkeyShiftID,MOD_SHIFT|MOD_WIN,0);
		g_bHotkeyShift=true;
	}
	else if (g_bHotkeyShift)
	{
		UnregisterHotKey(g_TaskBar,g_HotkeyShiftID);
		g_bHotkeyShift=false;
	}

	if (g_HotkeyCSM)
		UnregisterHotKey(g_TaskBar,g_HotkeyCSMID);
	g_HotkeyCSM=0;

	if (g_HotkeyWSM)
		UnregisterHotKey(g_TaskBar,g_HotkeyWSMID);
	g_HotkeyWSM=0;

	if (enable==HOTKEYS_NORMAL)
	{
		g_HotkeyCSM=GetSettingInt(L"CSMHotkey");
		if (g_HotkeyCSM)
		{
			int mod=MOD_NOREPEAT;
			if (g_HotkeyCSM&(HOTKEYF_SHIFT<<8)) mod|=MOD_SHIFT;
			if (g_HotkeyCSM&(HOTKEYF_CONTROL<<8)) mod|=MOD_CONTROL;
			if (g_HotkeyCSM&(HOTKEYF_ALT<<8)) mod|=MOD_ALT;
			RegisterHotKey(g_TaskBar,g_HotkeyCSMID,mod,g_HotkeyCSM&255);
		}

		g_HotkeyWSM=GetSettingInt(L"WSMHotkey");
		if (g_HotkeyWSM)
		{
			int mod=MOD_NOREPEAT;
			if (g_HotkeyWSM&(HOTKEYF_SHIFT<<8)) mod|=MOD_SHIFT;
			if (g_HotkeyWSM&(HOTKEYF_CONTROL<<8)) mod|=MOD_CONTROL;
			if (g_HotkeyWSM&(HOTKEYF_ALT<<8)) mod|=MOD_ALT;
			RegisterHotKey(g_TaskBar,g_HotkeyWSMID,mod,g_HotkeyWSM&255);
		}
	}
}

static void UpdateStartButtonPosition(const TaskbarInfo* taskBar, const WINDOWPOS* pPos)
{
	if (IsStartButtonSmallIcons(taskBar->taskbarId) != IsTaskbarSmallIcons())
		RecreateStartButton(taskBar->taskbarId);

	RECT rcTask;
	GetWindowRect(taskBar->taskBar, &rcTask);
	MONITORINFO info;
	UINT uEdge = GetTaskbarPosition(taskBar->taskBar, &info, NULL, NULL);
	DWORD buttonFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE;
	if (IsWindowVisible(taskBar->taskBar))
		buttonFlags |= SWP_SHOWWINDOW;
	else
		buttonFlags |= SWP_HIDEWINDOW;

	APPBARDATA appbar = { sizeof(appbar) };
	if (SHAppBarMessage(ABM_GETSTATE, &appbar) & ABS_AUTOHIDE)
	{
		bool bHide = false;
		if (uEdge == ABE_LEFT)
			bHide = (rcTask.right < info.rcMonitor.left + 5);
		else if (uEdge == ABE_RIGHT)
			bHide = (rcTask.left > info.rcMonitor.right - 5);
		else if (uEdge == ABE_TOP)
			bHide = (rcTask.bottom < info.rcMonitor.top + 5);
		else
			bHide = (rcTask.top > info.rcMonitor.bottom - 5);
		if (bHide)
			buttonFlags = (buttonFlags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
	}
	if (uEdge == ABE_TOP || uEdge == ABE_BOTTOM)
	{
		if (rcTask.left < info.rcMonitor.left) rcTask.left = info.rcMonitor.left;
		if (rcTask.right > info.rcMonitor.right) rcTask.right = info.rcMonitor.right;
	}
ÿ
