#ifdef DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

#include <stdlib.h>
#include <malloc.h>
#include <crtdbg.h>

#include <windows.h>
#include <process.h>
#include <d3d9.h>
#include <math.h>
#include <dwmapi.h>

// SPOUT
#include <ShellScalingApi.h> // for dpi awareness
#pragma comment(lib, "shcore.lib") // for dpi awareness

#include "plugin.h"
#include "resource.h"
#include "pluginshell.h"

#include <mutex>
#include <atomic>

//#include <core/sdk/constants.h>
//#include <core/sdk/IPcmVisualizer.h>
//#include <core/sdk/IPlaybackRemote.h>

#include "..\audio\common.h"

#define DLL_EXPORT __declspec(dllexport)
//#define COMPILE_AS_DLL
#define SAMPLE_SIZE 576
#define DEFAULT_WIDTH 800;
#define DEFAULT_HEIGHT 800;

CPlugin g_plugin;
HINSTANCE api_orig_hinstance = nullptr;
_locale_t g_use_C_locale;
char keyMappings[8];

// SPOUT
// ===============================================
static int nBeatDrops = 0; // Number of beatdrop instances already running
BOOL CALLBACK GetWindowNames(HWND h, LPARAM l); // Window enumerator callback
// ===============================================

// SPOUT - DX9EX
static IDirect3D9Ex* pD3D9 = nullptr;
static IDirect3DDevice9Ex* pD3DDevice = nullptr;

static D3DPRESENT_PARAMETERS d3dPp;

static LONG lastWindowStyle = 0;
static LONG lastWindowStyleEx = 0;

static bool fullscreen = false;
static bool stretch = false;
static bool borderless = false;
static RECT lastRect = { 0 };

static HMODULE module = nullptr;
static std::atomic<HANDLE> thread = nullptr;
static unsigned threadId = 0;
static std::mutex pcmMutex;
static unsigned char pcmLeftIn[SAMPLE_SIZE];
static unsigned char pcmRightIn[SAMPLE_SIZE];
static unsigned char pcmLeftOut[SAMPLE_SIZE];
static unsigned char pcmRightOut[SAMPLE_SIZE];

//static musik::core::sdk::IPlaybackService* playback = nullptr;

static HICON icon = nullptr;


// SPOUT
// ===============================================
BOOL CALLBACK GetWindowNames(HWND h, LPARAM l)
{
	if (h == NULL)
		return FALSE;

	char search_window_name[MAX_PATH];

	if (IsWindow(h) && IsWindowVisible(h)) {
		GetWindowTextA(h, search_window_name, MAX_PATH);
		if(search_window_name[0]) {
			// Does the search window name contain "BeatDrop Music Visualizer" ?
			if (strstr(search_window_name, "XorPlayer") != NULL) {
				nBeatDrops++;
			}
		}
	}
	return TRUE;
}
// ===============================================

// SPOUT - DX9EX
void InitD3d(HWND hwnd, int width, int height) {

	// TODO - error return
	HRESULT Hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D9);

	D3DCAPS9 d3dCaps;

	D3DDISPLAYMODEEX mode;
	D3DDISPLAYROTATION rot;
	pD3D9->GetAdapterDisplayModeEx(D3DADAPTER_DEFAULT, &mode, &rot);

    UINT adapterId = g_plugin.m_adapterId;

    if (adapterId > pD3D9->GetAdapterCount()) {
        adapterId = D3DADAPTER_DEFAULT;
    }

    memset(&d3dPp, 0, sizeof(d3dPp));

    d3dPp.BackBufferCount = 1;
	d3dPp.BackBufferFormat = D3DFMT_UNKNOWN;// mode.Format;
    d3dPp.BackBufferWidth = width;
    d3dPp.BackBufferHeight = height;
    d3dPp.SwapEffect = D3DSWAPEFFECT_COPY;
    d3dPp.Flags = 0;
	d3dPp.EnableAutoDepthStencil = FALSE;// TRUE;
	d3dPp.AutoDepthStencilFormat = D3DFMT_D24S8;// D3DFMT_D24X8;
    d3dPp.Windowed = TRUE;
    d3dPp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    d3dPp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dPp.hDeviceWindow = (HWND) hwnd;

	// Test for hardware vertex processing capability and set up as needed
	// D3DCREATE_MULTITHREADED required by interop spec
	if (pD3D9->GetDeviceCaps(adapterId, D3DDEVTYPE_HAL, &d3dCaps) != S_OK) {
		printf("Milkdrop2PcmVisualizer::CreateDX9device - GetDeviceCaps error\n");
		return;
	}

	DWORD dwBehaviorFlags = D3DCREATE_PUREDEVICE | D3DCREATE_MULTITHREADED;
	if (d3dCaps.VertexProcessingCaps != 0)
		dwBehaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		dwBehaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	Hr = pD3D9->CreateDeviceEx(
        adapterId,
        D3DDEVTYPE_HAL,
        (HWND) hwnd,
		dwBehaviorFlags,
        &d3dPp,
		NULL,
        &pD3DDevice);
}

void DeinitD3d() {
    if (pD3DDevice) {
        pD3DDevice->Release();
        pD3DDevice = nullptr;
    }

    if (pD3D9) {
        pD3D9->Release();
        pD3D9 = nullptr;
    }
}

//Multiple monitor stretch - Credit to @milkdropper for the code!
void ToggleStretch(HWND hwnd) {
    if (!stretch) {
        ShowCursor(FALSE);
        int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int top = GetSystemMetrics(SM_YVIRTUALSCREEN);

        if (!fullscreen) {
            lastWindowStyle = GetWindowLong(hwnd, GWL_STYLE);
            lastWindowStyleEx = GetWindowLongW(hwnd, GWL_EXSTYLE);
            lastWindowStyleEx &= ~WS_EX_TOPMOST;
            GetWindowRect(hwnd, &lastRect);
        }

        d3dPp.BackBufferWidth = width;
        d3dPp.BackBufferHeight = height;

        pD3DDevice->Reset(&d3dPp);

        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, left, top, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
		DragAcceptFiles(hwnd, TRUE);

        stretch = true;
    }
    else {
        ShowCursor(TRUE);

        int x = lastRect.left;
        int y = lastRect.top;
        int width = lastRect.right - lastRect.left;
        int height = lastRect.bottom - lastRect.top;

        d3dPp.BackBufferWidth = width;
        d3dPp.BackBufferHeight = height;

        pD3DDevice->Reset(&d3dPp);
        SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
        stretch = false;

        SetWindowLongW(hwnd, GWL_STYLE, lastWindowStyle);
        SetWindowLongW(hwnd, GWL_EXSTYLE, lastWindowStyleEx);
        SetWindowPos(hwnd, HWND_NOTOPMOST, lastRect.left, lastRect.top, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        if (borderless)
        {
            SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        }
    }
    fullscreen = false;
}

void ToggleFullScreen(HWND hwnd) {
    if (!fullscreen) {
        ShowCursor(FALSE);

        if (!stretch) {
            lastWindowStyle = GetWindowLong(hwnd, GWL_STYLE);
            lastWindowStyleEx = GetWindowLongW(hwnd, GWL_EXSTYLE);
            lastWindowStyleEx &= ~WS_EX_TOPMOST;
            GetWindowRect(hwnd, &lastRect);
        }

        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        MONITORINFO info;
        info.cbSize = sizeof(MONITORINFO);

        GetMonitorInfoW(monitor, &info);

        int width = info.rcMonitor.right - info.rcMonitor.left;
        int height = info.rcMonitor.bottom - info.rcMonitor.top;

        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
        SetWindowPos(hwnd, HWND_TOPMOST, info.rcMonitor.left, info.rcMonitor.top, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);

        d3dPp.BackBufferWidth = width;
        d3dPp.BackBufferHeight = height;

        pD3DDevice->Reset(&d3dPp);
        SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
		DragAcceptFiles(hwnd, TRUE);
        fullscreen = true;
    }
    else {
        ShowCursor(TRUE);

        int x = lastRect.left;
        int y = lastRect.top;
        int width = lastRect.right - lastRect.left;
        int height = lastRect.bottom - lastRect.top;

        d3dPp.BackBufferWidth = width;
        d3dPp.BackBufferHeight = height;

        pD3DDevice->Reset(&d3dPp);
        SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
        fullscreen = false;

        SetWindowLongW(hwnd, GWL_STYLE, lastWindowStyle);
        SetWindowLongW(hwnd, GWL_EXSTYLE, lastWindowStyleEx);
        SetWindowPos(hwnd, HWND_NOTOPMOST, lastRect.left, lastRect.top, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        if (borderless)
        {
            SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        }
    }
     stretch = false;
}
void ToggleBorderlessWindow(HWND hwnd)
{
    if (!borderless)
    {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int x = rect.left;
        int y = rect.top;
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        borderless = true;
    }
    else {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int x = rect.left;
        int y = rect.top;
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, width, height, SWP_DRAWFRAME | SWP_FRAMECHANGED);
        borderless = false;
    }
}


LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            
	switch(uMsg) {
            
        case WM_CLOSE: {
            DestroyWindow( hWnd );
            UnregisterClassW(L"Direct3DWindowClass", NULL);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }

        case WM_KEYDOWN: {
            /*if (playback && wParam >= VK_F1 && wParam <= VK_F8) {
                switch (wParam) {
                case VK_F1:
                    playback->PauseOrResume();
                    break;
                case VK_F2:
                    playback->Stop();
                    break;
                case VK_F3:
                    playback->Previous();
                    break;
                case VK_F4:
                    playback->Next();
                    break;
                case VK_F5:
                    playback->SetVolume(playback->GetVolume() - 0.05);
                    break;
                case VK_F6:
                    playback->SetVolume(playback->GetVolume() + 0.05);
                    break;
                case VK_F7:
                    playback->ToggleShuffle();
                    break;
                case VK_F8:
                    playback->ToggleMute();
                    break;
                }
            }*/
            g_plugin.PluginShellWindowProc(hWnd, uMsg, wParam, lParam);
        }
        if (wParam == VK_F6)
        {
            if (!fullscreen && !stretch)
            ToggleBorderlessWindow(hWnd);
        }
        break;

        case WM_NCHITTEST: //used for borderless window
        {
            if (borderless)
                if (!fullscreen && !stretch)
            {
                //resizable borderless
                //from https://stackoverflow.com/questions/19106047/winapi-c-reprogramming-window-resize
                    #define BORDERWIDTH  10
                    #define TITLEBARWIDTH  30
                    RECT rect;
                    int x, y;
                    GetWindowRect(hWnd, &rect);

                    x = GET_X_LPARAM(lParam) - rect.left;
                    y = GET_Y_LPARAM(lParam) - rect.top;

                    if (x >= BORDERWIDTH && x <= rect.right - rect.left - BORDERWIDTH && y >= BORDERWIDTH && y <= TITLEBARWIDTH)
                        return HTCAPTION;

                    else if (x < BORDERWIDTH && y < BORDERWIDTH)
                        return HTTOPLEFT;
                    else if (x > rect.right - rect.left - BORDERWIDTH && y < BORDERWIDTH)
                        return HTTOPRIGHT;
                    else if (x > rect.right - rect.left - BORDERWIDTH && y > rect.bottom - rect.top - BORDERWIDTH)
                        return HTBOTTOMRIGHT;
                    else if (x < BORDERWIDTH && y > rect.bottom - rect.top - BORDERWIDTH)
                        return HTBOTTOMLEFT;

                    else if (x < BORDERWIDTH)
                        return HTLEFT;
                    else if (y < BORDERWIDTH)
                        return HTTOP;
                    else if (x > rect.right - rect.left - BORDERWIDTH)
                        return HTRIGHT;
                    else if (y > rect.bottom - rect.top - BORDERWIDTH)
                        return HTBOTTOM;
                return HTCAPTION;
            case WM_NCLBUTTONDBLCLK:
            {
            if (borderless)
                 ToggleFullScreen(hWnd);
                 break;
            }
        }
        break;
        }

        case WM_SYSKEYDOWN: {
            if (wParam == VK_F4) {
                PostQuitMessage(0);
            }
             else if (wParam == VK_SHIFT)
            {
                ToggleStretch(hWnd);
            }
            else if (wParam == VK_RETURN) {
                ToggleFullScreen(hWnd);
            }
            else {
                g_plugin.PluginShellWindowProc(hWnd, uMsg, wParam, lParam);
            }
            break;
        }

        case WM_LBUTTONDBLCLK:
        {
            ToggleFullScreen(hWnd);
            break;
        }


        default:
            return g_plugin.PluginShellWindowProc(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void RenderFrame() {
    {
        std::unique_lock<std::mutex> lock(pcmMutex);
        memcpy(pcmLeftOut, pcmLeftIn, SAMPLE_SIZE);
        memcpy(pcmRightOut, pcmRightIn, SAMPLE_SIZE);
        memset(pcmLeftIn, 0, SAMPLE_SIZE);
        memset(pcmRightIn, 0, SAMPLE_SIZE);
    }

    g_plugin.PluginRender(
        (unsigned char*) pcmLeftOut,
        (unsigned char*) pcmRightOut);
}

// SPOUT
void ResizeBeatDrop(int newWidth, int newHeight)
{
	// Client window size
	int width  = 640;
	int height = 360;
	// If the window is square - 512x512, otherwise 640x360
	if (newWidth == newHeight) {
		width  = 512;
		height = 512;
	}
	HWND hw = FindWindowA("Direct3DWindowClass", "BeatDrop Music Visualizer");
	// Client to window
	RECT rc;
	SetRect(&rc, 0, 0, width, height);
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);
	width = rc.right - rc.left;
	height = rc.bottom - rc.top;
	// Resize but do not move
	SetWindowPos(hw, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
}


unsigned __stdcall CreateWindowAndRun(void* data) {
    HINSTANCE instance = (HINSTANCE) data;

#ifdef DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetBreakAlloc(60);
#endif

	// SPOUT
	// Set Per Monitor awareness
	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE); //older Windows versions: Entry Point Not Found Fix
	// SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    // Register the windows class
    WNDCLASSW wndClass;
    wndClass.style = CS_DBLCLKS;
    wndClass.lpfnWndProc = StaticWndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = instance;
    wndClass.hIcon = NULL;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = L"Direct3DWindowClass";

    if (!RegisterClassW(&wndClass)) {
        DWORD dwError = GetLastError();
        if (dwError != ERROR_CLASS_ALREADY_EXISTS) {
            return 0;
        }
    }

    // SPOUT
	// make the window a fixed size to and avoid
	// resolution change for move instead of size
    int windowWidth = 800;
    int windowHeight = 800;

    RECT rc;
    SetRect(&rc, 0, 0, windowWidth, windowHeight);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);
    
    // SPOUT
	// Centre on the desktop work area
	int WindowPosLeft = 0;
	int WindowPosTop = 0;
	RECT WorkArea;
	SystemParametersInfo(SPI_GETWORKAREA, 0, (LPVOID)&WorkArea, 0);
	WindowPosLeft += ((WorkArea.right - WorkArea.left) - windowWidth) / 2;
	WindowPosTop += ((WorkArea.bottom - WorkArea.top) - windowHeight) / 2;

	// SPOUT
	// Remove minimize and maximize
	DWORD dwStyle = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME);

    // Create the render window
    HWND hwnd = CreateWindowW(
        L"Direct3DWindowClass",
        L"XorPlayer",
        // WS_OVERLAPPEDWINDOW, // SPOUT
		dwStyle,
		WindowPosLeft, // SPOUT
		WindowPosTop,
		// CW_USEDEFAULT,
		// CW_USEDEFAULT,
        (rc.right - rc.left),
        (rc.bottom - rc.top),
        0,
        NULL,
        instance,
        0);

    if (!hwnd) {
        DWORD dwError = GetLastError();
        return 0;
    }

    if (!icon) {
        icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PLUGIN_ICON));
    }

    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM) icon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) icon);

    ShowWindow(hwnd, SW_SHOW);

	unsigned int frame = 0;

	// SPOUT - defaults if no GUI
    // Change the values here to set the fixed sender size
	unsigned int SpoutWidth = 720;
	unsigned int SpoutHeight = 720;

    // Set to windowWidth/windowHeight for a variable sender size
    // See milkDropfs.cpp RenderFrame - change to SendDX9surface(back_buffer, true); 
    // SpoutWidth = windowWidth;
    // SpoutHeight = windowHeight;

	unsigned int SpoutWidthOld = 0;
	unsigned int SpoutHeightOld = 0;
	// Read values if UI is open
	SpoutWidthOld = SpoutWidth;
	SpoutHeightOld = SpoutHeight;

    g_plugin.PluginPreInitialize(0, 0);

    // SPOUT
    // InitD3d(hwnd, windowWidth, windowHeight);
	InitD3d(hwnd, SpoutWidth, SpoutHeight);

    g_plugin.PluginInitialize(
        pD3DDevice,
        &d3dPp,
        hwnd,
        // windowWidth,
        // windowHeight);
		SpoutWidth,
		SpoutHeight);

	// g_plugin. .m_lpVS->

    MSG msg;
    msg.message = WM_NULL;

    PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
    while (WM_QUIT != msg.message) {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            GetAudioBuf(pcmLeftIn, pcmRightIn, SAMPLE_SIZE);
            RenderFrame();
        }
		frame++;
		////////////////////////////////////////////////////////////////////////////////////////////////
    }

    g_plugin.MyWriteConfig();
    g_plugin.PluginQuit();

    DeinitD3d();

    thread = nullptr;
    threadId = 0;

    return 1;
}

void StartRenderThread(HINSTANCE instance) {
    thread = (HANDLE) _beginthreadex(
        nullptr,
        0,
        &CreateWindowAndRun,
        (void *) instance,
        0,
        &threadId);
}

int StartThreads(HINSTANCE instance) {

    HRESULT hr = S_OK;

    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        ERR(L"CoInitialize failed: hr = 0x%08x", hr);
        return -__LINE__;
    }
    CoUninitializeOnExit cuoe;

    // argc==1 No additional params. Output disabled.
    // argc==3 Two additional params. Output file enabled (32bit IEEE 754 FLOAT).
    // argc==4 Three additional params. Output file enabled (LITTLE ENDIAN PCM).
    int argc = 1;
    LPCWSTR argv[4] = { L"", L"--file", L"loopback-capture.wav", L"--int-16" };
    hr = S_OK;

    // parse command line
    CPrefs prefs(argc, argv, hr);
    if (FAILED(hr)) {
        ERR(L"CPrefs::CPrefs constructor failed: hr = 0x%08x", hr);
        return -__LINE__;
    }
    if (S_FALSE == hr) {
        // nothing to do
        return 0;
    }

    // create a "loopback capture has started" event
    HANDLE hStartedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (NULL == hStartedEvent) {
        ERR(L"CreateEvent failed: last error is %u", GetLastError());
        return -__LINE__;
    }
    CloseHandleOnExit closeStartedEvent(hStartedEvent);

    // create a "stop capturing now" event
    HANDLE hStopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (NULL == hStopEvent) {
        ERR(L"CreateEvent failed: last error is %u", GetLastError());
        return -__LINE__;
    }
    CloseHandleOnExit closeStopEvent(hStopEvent);

    // create arguments for loopback capture thread
    LoopbackCaptureThreadFunctionArguments threadArgs;
    threadArgs.hr = E_UNEXPECTED; // thread will overwrite this
    threadArgs.pMMDevice = prefs.m_pMMDevice;
    threadArgs.bInt16 = prefs.m_bInt16;
    threadArgs.hFile = prefs.m_hFile;
    threadArgs.hStartedEvent = hStartedEvent;
    threadArgs.hStopEvent = hStopEvent;
    threadArgs.nFrames = 0;

    HANDLE hThread = CreateThread(
        NULL, 0,
        LoopbackCaptureThreadFunction, &threadArgs,
        0, NULL
    );
    if (NULL == hThread) {
        ERR(L"CreateThread failed: last error is %u", GetLastError());
        return -__LINE__;
    }
    CloseHandleOnExit closeThread(hThread);

    // wait for either capture to start or the thread to end
    HANDLE waitArray[2] = { hStartedEvent, hThread };
    DWORD dwWaitResult;
    dwWaitResult = WaitForMultipleObjects(
        ARRAYSIZE(waitArray), waitArray,
        FALSE, INFINITE
    );

    if (WAIT_OBJECT_0 + 1 == dwWaitResult) {
        ERR(L"Thread aborted before starting to loopback capture: hr = 0x%08x", threadArgs.hr);
        return -__LINE__;
    }

    if (WAIT_OBJECT_0 != dwWaitResult) {
        ERR(L"Unexpected WaitForMultipleObjects return value %u", dwWaitResult);
        return -__LINE__;
    }

    // at this point capture is running
    // wait for the user to press a key or for capture to error out
    
    /*HANDLE thread =*/ StartRenderThread(instance);
    WaitForSingleObject(thread, INFINITE);

    //NEED TO STOP CAPTURE
    // at this point capture is running
    // wait for the user to press a key or for capture to error out
    {
        WaitForSingleObjectOnExit waitForThread(hThread);
        SetEventOnExit setStopEvent(hStopEvent);
        HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);

        if (INVALID_HANDLE_VALUE == hStdIn) {
            ERR(L"GetStdHandle returned INVALID_HANDLE_VALUE: last error is %u", GetLastError());
            return -__LINE__;
        }

        LOG(L"%s", L"Press Enter to quit...");

        HANDLE rhHandles[2] = { hThread, hStdIn };

        bool bKeepWaiting = true;
        while (bKeepWaiting) {

            dwWaitResult = WaitForMultipleObjects(2, rhHandles, FALSE, INFINITE);

            switch (dwWaitResult) {

            case WAIT_OBJECT_0: // hThread
                ERR(L"%s", L"The thread terminated early - something bad happened");
                bKeepWaiting = false;
                break;

            case WAIT_OBJECT_0 + 1: // hStdIn
                                    // see if any of them was an Enter key-up event
                /*INPUT_RECORD rInput[128];
                DWORD nEvents;
                if (!ReadConsoleInput(hStdIn, rInput, ARRAYSIZE(rInput), &nEvents)) {
                    ERR(L"ReadConsoleInput failed: last error is %u", GetLastError());
                    bKeepWaiting = false;
                }
                else {
                    for (DWORD i = 0; i < nEvents; i++) {
                        if (
                            KEY_EVENT == rInput[i].EventType &&
                            VK_RETURN == rInput[i].Event.KeyEvent.wVirtualKeyCode &&
                            !rInput[i].Event.KeyEvent.bKeyDown
                            ) {*/
                            LOG(L"%s", L"Stopping capture...");
                            bKeepWaiting = false;
                            break;
                /*        }
                    }
                    // if none of them were Enter key-up events,
                    // continue waiting
                }*/
                break;

            default:
                ERR(L"WaitForMultipleObjects returned unexpected value 0x%08x", dwWaitResult);
                bKeepWaiting = false;
                break;
            } // switch
        } // while
    } // naked scope

    // at this point the thread is definitely finished

    DWORD exitCode;
    if (!GetExitCodeThread(hThread, &exitCode)) {
        ERR(L"GetExitCodeThread failed: last error is %u", GetLastError());
        return -__LINE__;
    }

    if (0 != exitCode) {
        ERR(L"Loopback capture thread exit code is %u; expected 0", exitCode);
        return -__LINE__;
    }

    if (S_OK != threadArgs.hr) {
        ERR(L"Thread HRESULT is 0x%08x", threadArgs.hr);
        return -__LINE__;
    }

    if (NULL != prefs.m_szFilename) {
        // everything went well... fixup the fact chunk in the file
        MMRESULT result = mmioClose(prefs.m_hFile, 0);
        prefs.m_hFile = NULL;
        if (MMSYSERR_NOERROR != result) {
            ERR(L"mmioClose failed: MMSYSERR = %u", result);
            return -__LINE__;
        }

        // reopen the file in read/write mode
        MMIOINFO mi = { 0 };
        prefs.m_hFile = mmioOpenW(const_cast<LPWSTR>(prefs.m_szFilename), &mi, MMIO_READWRITE);
        if (NULL == prefs.m_hFile) {
            ERR(L"mmioOpen(\"%ls\", ...) failed. wErrorRet == %u", prefs.m_szFilename, mi.wErrorRet);
            return -__LINE__;
        }

        // descend into the RIFF/WAVE chunk
        MMCKINFO ckRIFF = { 0 };
        ckRIFF.ckid = MAKEFOURCC('W', 'A', 'V', 'E'); // this is right for mmioDescend
        result = mmioDescend(prefs.m_hFile, &ckRIFF, NULL, MMIO_FINDRIFF);
        if (MMSYSERR_NOERROR != result) {
            ERR(L"mmioDescend(\"WAVE\") failed: MMSYSERR = %u", result);
            return -__LINE__;
        }

        // descend into the fact chunk
        MMCKINFO ckFact = { 0 };
        ckFact.ckid = MAKEFOURCC('f', 'a', 'c', 't');
        result = mmioDescend(prefs.m_hFile, &ckFact, &ckRIFF, MMIO_FINDCHUNK);
        if (MMSYSERR_NOERROR != result) {
            ERR(L"mmioDescend(\"fact\") failed: MMSYSERR = %u", result);
            return -__LINE__;
        }

        // write the correct data to the fact chunk
        LONG lBytesWritten = mmioWrite(
            prefs.m_hFile,
            reinterpret_cast<PCHAR>(&threadArgs.nFrames),
            sizeof(threadArgs.nFrames)
        );
        if (lBytesWritten != sizeof(threadArgs.nFrames)) {
            ERR(L"Updating the fact chunk wrote %u bytes; expected %u", lBytesWritten, (UINT32)sizeof(threadArgs.nFrames));
            return -__LINE__;
        }

        // ascend out of the fact chunk
        result = mmioAscend(prefs.m_hFile, &ckFact, 0);
        if (MMSYSERR_NOERROR != result) {
            ERR(L"mmioAscend(\"fact\") failed: MMSYSERR = %u", result);
            return -__LINE__;
        }
    }

    // let prefs' destructor call mmioClose

    return 0;
}

// SPOUT
//
// Test for DirectX installation and warn if not installed
// TODO : doesn't seem to need a header declaration
//
// Registry method only works for DirectX 9 and lower but that is OK
bool CheckForDirectX9c()
{

	// HKLM\Software\Microsoft\DirectX\Version should be 4.09.00.0904
	// handy information : http://en.wikipedia.org/wiki/DirectX
	HKEY  hRegKey;
	LONG  regres;
	DWORD  dwSize, major, minor, revision, notused;
	char value[256];
	dwSize = 256;

	// Does the key exist
	regres = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\DirectX", NULL, KEY_READ, &hRegKey);
	if (regres == ERROR_SUCCESS) {
		// Read the key
		regres = RegQueryValueExA(hRegKey, "Version", 0, NULL, (LPBYTE)value, &dwSize);
		// Decode the string : 4.09.00.0904
		sscanf_s(value, "%d.%d.%d.%d", &major, &minor, &notused, &revision);
		// printf("DirectX registry : [%s] (%d.%d.%d.%d)\n", value, major, minor, notused, revision);
		RegCloseKey(hRegKey);
		if (major == 4 && minor == 9 && revision == 904)
			return true;
	}

	return false;

}


#ifdef COMPILE_AS_DLL
    BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
        module = hModule;
        api_orig_hinstance = hModule;
        return true;
    }
#else
    int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow) {
        api_orig_hinstance = hInstance;
		// SPOUT
		if (CheckForDirectX9c())
        return StartThreads(hInstance);
		else
			return false;
    }
#endif

static std::string title;
/*
class VisaulizerPlugin : public musik::core::sdk::IPlugin {
    public:
        virtual void Release() override { }
        virtual const char* Name() override { return "Milkdrop2 IPcmVisualizer, IPlaybackRemote"; }
        virtual const char* Version() override { return "0.5.3"; }
        virtual const char* Author() override { return "clangen"; }
        virtual const char* Guid() override { return "5533c371-ed2b-40cf-aabb-f897661aeec1"; }
        virtual bool Configurable() override { return false; }
        virtual void Configure() override { }
        virtual void Reload() override { }
        virtual int SdkVersion() override { return musik::core::sdk::SdkVersion; }
};

class Visualizer :
    public musik::core::sdk::IPcmVisualizer ,
    public musik::core::sdk::IPlaybackRemote {
        public:
            virtual const char* Name() override {
                return "Milkdrop2";
            }

            virtual void Release() override {
                this->Hide();
            }

            virtual void SetPlaybackService(musik::core::sdk::IPlaybackService* playback) override {
                g_plugin.playbackService = playback;
                ::playback = playback;
            }

            virtual void OnTrackChanged(musik::core::sdk::ITrack* track) override {
                if (track) {
                    char buffer[1024];
                    track->GetString("title", buffer, 1024);
                    g_plugin.emulatedWinampSongTitle = std::string(buffer);
                }
                else {
                    g_plugin.emulatedWinampSongTitle = "";
                }
            }

            virtual void OnPlaybackStateChanged(musik::core::sdk::PlaybackState state) override {

            }

            virtual void OnVolumeChanged(double volume) override {

            }

            virtual void OnModeChanged(musik::core::sdk::RepeatMode repeatMode, bool shuffled) override {

            }

            virtual void OnPlayQueueChanged() override {

            }

            virtual void OnPlaybackTimeChanged(double time) override {

            }

            virtual void Write(musik::core::sdk::IBuffer* buffer) override {
                if (Visible()) {
                    float* b = buffer->BufferPointer();

                    std::unique_lock<std::mutex> lock(pcmMutex);

                    int n = 0;
                    for (int i = 0; i < buffer->Samples(); i++, n++) {
                        int x = i * 2;
                        pcmLeftIn[n % SAMPLE_SIZE] = (unsigned char)(b[i + 0] * 255.0f);
                        pcmRightIn[n % SAMPLE_SIZE] = (unsigned char)(b[i + 1] * 255.0f);
                    }
                }
            }

            virtual void Show() override {
                if (!Visible()) {
                    StartRenderThread(module);
                }
            }

            virtual void Hide() override {
                if (Visible()) {
                    PostThreadMessage(threadId, WM_QUIT, 0, 0);
                    WaitForSingleObject(thread, INFINITE);
                }
            }

            virtual bool Visible() override {
                return thread.load() != nullptr;
            }
};

static VisaulizerPlugin visualizerPlugin;
static Visualizer visualizer;

extern "C" DLL_EXPORT musik::core::sdk::IPlugin* GetPlugin() {
    return &visualizerPlugin;
}

extern "C" DLL_EXPORT musik::core::sdk::IPcmVisualizer* GetPcmVisualizer() {
    return &visualizer;
}

extern "C" DLL_EXPORT musik::core::sdk::IPlaybackRemote* GetPlaybackRemote() {
    return &visualizer;
}
*/
#ifdef DEBUG
struct _DEBUG_STATE {
    _DEBUG_STATE() {
    }

    ~_DEBUG_STATE() {
        _CrtDumpMemoryLeaks();
    }
};

_DEBUG_STATE ds;
#endif
