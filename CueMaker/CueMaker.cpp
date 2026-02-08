// CueMaker.cpp : 定义应用程序的入口点。
//
#define _CRT_SECURE_NO_WARNINGS
#include "framework.h"
#include "CueMaker.h"
#include <commctrl.h>
#include <string>
#include <cstdlib>
#include <ctime>
#define TIMER_PLAY  3300

// ===== 新增：工具栏所需库链接 =====
#pragma comment(lib, "comctl32.lib")

HWND g_hListView = nullptr;
HWND g_hWaveView = nullptr;

const int LEFT_PANEL_WIDTH = 220;
// 波形控件全局参数（双声道+缩放/偏移控制）
double g_waveZoomX = 1.0;       // X轴缩放比例（控制时间轴拉长/缩短）
double g_waveOffsetX = 0.0;     // X轴偏移量（控制显示区间）
int g_waveClickPosX = -1;       // CUE标记X坐标
int CHANNEL_COUNT = 2;    // 双声道（左/右）
int CHANNEL_SPACING = 30; // 双声道之间的间距
LastSessionState  workState;
extern std::wstring g_cueFileContent;
extern WorkContext g_Work;
extern std::wstring extractFileName;
extern std::wstring g_apeFilePath, g_cueFilePath;
HWND g_hTrackList = NULL;
extern bool g_isPlaying ;
extern int clickX, g_currentTimeMs, g_musicLengthMs;
extern HWND hTime, hTrack;



// 全局变量:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

// ===== 新增：双工具栏全局资源（分开管理，避免混淆）=====
HBITMAP g_hToolbarTopBmp = NULL;    // 顶部工具栏位图
HIMAGELIST g_hImageListTop = NULL;  // 顶部工具栏图片列表
HBITMAP g_hToolbarBottomBmp = NULL; // 底部工具栏位图
HIMAGELIST g_hImageListBottom = NULL; // 底部工具栏图片列表

// 函数声明
ATOM                MyRegisterClass(HINSTANCE hInstance);
ATOM                RegisterWaveCtrlClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // ===== 修改：扩展通用控件初始化，添加工具栏类（ICC_BAR_CLASSES）=====
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    srand((unsigned int)time(nullptr));

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_CUEMAKER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    RegisterWaveCtrlClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CUEMAKER));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CUEMAKER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_CUEMAKER);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

ATOM RegisterWaveCtrlClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);
    // 关键：添加CS_OWNDC减少DC创建开销，优化闪烁
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_GLOBALCLASS | CS_OWNDC;
    wcex.lpfnWndProc = WaveCtrlProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = nullptr;
    wcex.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = WAVE_CTRL_CLASS;
    wcex.hIconSm = nullptr;

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW| WS_CLIPCHILDREN,
        CW_USEDEFAULT, 0, 1200, 800, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 定义常量，方便统一修改布局
    const int PADDING = 10; // 边距
	static int toptool_height = 0;

    switch (message)
    {
    case WM_CREATE:
    {
        hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        // 1. 创建工具栏
        HWND hTop=CreateToolbarTop(hWnd, hInst);
        HWND hBot=CreateToolbarBottom(hWnd, hInst);
        RECT rc;
        GetClientRect(hTop, &rc);
        toptool_height = abs(rc.top - rc.bottom);
        UINT_PTR timerId = SetTimer(hWnd, TIMER_PLAY, 20, NULL); // 20ms 更新一次
        g_hTrackList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, toptool_height, 0, 0, hWnd, (HMENU)10101, hInst, nullptr);
        g_hWaveView = CreateWindowExW(WS_EX_CLIENTEDGE, WAVE_CTRL_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, toptool_height, 0, 0, hWnd, (HMENU)10102, hInst, nullptr);
        return 0;
    }
    case WM_TIMER:
    {
        int s = 0;
        if (wParam == TIMER_PLAY && g_isPlaying)
        {            
            if (g_currentTimeMs > g_musicLengthMs)
                g_currentTimeMs = g_musicLengthMs;

            // ===== 时间显示（不要放 WM_PAINT）=====
            wchar_t buf[32];

            int cur = g_currentTimeMs;      // 你现在的“时间单位”
            int min = cur / 60;
            int sec = cur % 60;

            int total = g_musicLengthMs;
            int totalMin = total / 60;
            int totalSec = total % 60;

            swprintf_s(buf, L"%02d:%02d / %02d:%02d",
                min, sec, totalMin, totalSec);

            SetWindowText(hTime, buf);

            InvalidateRect(g_hWaveView, NULL, FALSE);
        }

        return 0;
    }
    break;
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        // 处理 ListBox 选择事件
        if (wmId == 10101 && wmEvent == LBN_SELCHANGE)
        {
            int sel = (int)SendMessage(g_hTrackList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                // 执行你的波形跳转或状态更新逻辑
                g_waveOffsetX = 0.0;
                InvalidateRect(g_hWaveView, nullptr, FALSE);
            }
            return 0;
        }

        switch (wmId)
        {
        case ID_OPEN:
        {           
        }
        break;
        case ID_TB_AUTO_GENERATE:
            GenerateCueFile();
        break;
        case ID_TB_ALBUM_INFO:
        {
            ShowAlbumInfoDialog(hWnd);
        }
        break;
        case ID_TB_NEW_CUE:
        {
            if (OnNewCue(hWnd))
            {
                TCHAR szExeDir[512] = { 0 };
                GetWaveOrPcm(hWnd, g_Work.audioPath, 1);
                GetExeDirectory(szExeDir, 512);
                std::wstring cueMaker = std::wstring(szExeDir) + L"CueMakerState.ini";
                saveWorkStateToIni(cueMaker.c_str(), g_Work.audioPath.c_str(), NULL, NULL, 0);// 保存工作状态到INI文件
                LoadWavFile(extractFileName);
                InvalidateRect(g_hWaveView, nullptr, TRUE);
                UpdateWindow(g_hWaveView);
            }
        }
        break;
        case ID_TB_OPEN_CUE:
        {
        }
        break;
        case ID_TB_EDIT_CUE:
        {

        }
        break;
        case ID_TB_OPEN_AUDIO:
        {
        }
        break;
        case ID_TB_FIND_SILENCE:
        {

        }
        break;
        case ID_TB_SPLIT_TRACKS:
        {

        }
        break;
        case ID_TB_CONVERT_FORMAT:
        {

        }
        break;
        case ID_TB_UNDO:
        {

        }
        break;
        case ID_TB_SETTINGS:
        {

        }
        break;
        case ID_MUSIC_PLAY:
        {
            StartPlay(extractFileName.c_str());
            g_isPlaying = true;
        }
		break;
        case ID_MUSIC_PAUSE:
        {
             PausePlay();
		}   
		break;
        case ID_MUSIC_STOP:
        {
            StopPlay();
		}
		break;
        //case
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        }
    }
    break;

    case WM_SIZE:
    {
        int cx = LOWORD(lParam);
        int cy = HIWORD(lParam);
        if (cx <= 0 || cy <= 0) break;

        // 获取工具栏实际高度（如果 TOOLBAR_HEIGHT 是固定的则直接用）
        int topBarH = toptool_height;
        int bottomBarH = toptool_height;

        // 计算中间内容区的起始 Y 和总高度
        int contentY = topBarH;
        int contentH = cy - topBarH - bottomBarH;
        int a = toptool_height;
        if (contentH < 0) contentH = 0;

        // 使用 DeferWindowPos 减少重绘闪烁
        HDWP hdwp = BeginDeferWindowPos(4);

        // 1. 顶部工具栏
        if (HWND hTop = GetDlgItem(hWnd, IDC_TOOLBAR_TOP))
            hdwp = DeferWindowPos(hdwp, hTop, nullptr, 0, 0, cx, topBarH, SWP_NOZORDER);

        // 2. 底部工具栏
        if (HWND hBottom = GetDlgItem(hWnd, IDC_TOOLBAR_BOTTOM))
            hdwp = DeferWindowPos(hdwp, hBottom, nullptr, 0, cy - bottomBarH, cx, bottomBarH, SWP_NOZORDER);

        // 3. 左侧 ListBox
        if (g_hTrackList)
            hdwp = DeferWindowPos(hdwp, g_hTrackList, nullptr,
                0, contentY, LEFT_PANEL_WIDTH, contentH, SWP_NOZORDER);

        // 4. 右侧 WaveView (填充剩余宽度)
        if (g_hWaveView)
            hdwp = DeferWindowPos(hdwp, g_hWaveView, hWnd,
                LEFT_PANEL_WIDTH, contentY, cx - LEFT_PANEL_WIDTH, contentH, SWP_NOZORDER);

        EndDeferWindowPos(hdwp);

        // 不需要显式 InvalidateRect(hWnd)，子窗口会自动处理
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* pMinMax = (MINMAXINFO*)lParam;
        pMinMax->ptMinTrackSize.x = 600;
        pMinMax->ptMinTrackSize.y = 400;
        return 0;
    }

    case WM_DESTROY:
        // 资源清理...
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}