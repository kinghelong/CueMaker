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
extern bool g_isPlaying;
extern int clickX, g_currentTimeMs, g_musicLengthMs;
extern HWND hTime, hTrack;
extern wave_header g_wavHeader;
extern WAVEFORMATEX g_wav;
bool g_isDraggingTrackBar = false;
// TrackBar 子类化相关全局变量
extern WNDPROC g_pOldTrackBarProc ; // 保存TrackBar原始窗口过程
extern bool g_isDraggingTrackBar ;    // 拖动标记（仅作用于TrackBar）

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
// 声明缺失的函数（避免编译错误）
HWND CreateToolbarTop(HWND hWnd, HINSTANCE hInst);
HWND CreateToolbarBottom(HWND hWnd, HINSTANCE hInst);
void ShowAlbumInfoDialog(HWND hWnd);
bool OnNewCue(HWND hWnd);
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WaveCtrlProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 资源清理函数
void CleanupResources()
{
    // 释放图片列表和位图
    if (g_hImageListTop)
    {
        ImageList_Destroy(g_hImageListTop);
        g_hImageListTop = NULL;
    }
    if (g_hImageListBottom)
    {
        ImageList_Destroy(g_hImageListBottom);
        g_hImageListBottom = NULL;
    }
    if (g_hToolbarTopBmp)
    {
        DeleteObject(g_hToolbarTopBmp);
        g_hToolbarTopBmp = NULL;
    }
    if (g_hToolbarBottomBmp)
    {
        DeleteObject(g_hToolbarBottomBmp);
        g_hToolbarBottomBmp = NULL;
    }
}

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
        CleanupResources(); // 初始化失败时清理资源
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

    CleanupResources(); // 消息循环结束后清理资源
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
    // 移除 CS_OWNDC，添加 CS_DBLCLKS
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_GLOBALCLASS;
    wcex.lpfnWndProc = WaveCtrlProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = nullptr;
    wcex.hCursor = LoadCursor(nullptr, IDC_CROSS);
    // 关键：使用 NULL 背景，在 WM_PAINT 中自己处理，避免闪烁
    wcex.hbrBackground = nullptr;  // 改为 nullptr
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"WAVE_CTRL_CLASS"; // 定义具体的类名
    wcex.hIconSm = nullptr;

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
    static UINT_PTR g_timerPlay = 0; // 保存定时器ID

    switch (message)
    {
    case WM_CREATE:
    {
        hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        // 1. 创建工具栏
        HWND hTop = CreateToolbarTop(hWnd, hInst);
        HWND hBot = CreateToolbarBottom(hWnd, hInst);
        RECT rc;
        GetClientRect(hTop, &rc);
        toptool_height = abs(rc.top - rc.bottom);
        // 保存定时器ID，方便后续销毁
        g_timerPlay = SetTimer(hWnd, TIMER_PLAY, 20, NULL); // 20ms 更新一次
        g_hTrackList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, toptool_height, 0, 0, hWnd, (HMENU)10101, hInst, nullptr);
        g_hWaveView = CreateWindowExW(WS_EX_CLIENTEDGE, L"WAVE_CTRL_CLASS", L"", WS_CHILD | WS_VISIBLE, 0, toptool_height, 0, 0, hWnd, (HMENU)10102, hInst, nullptr);
        return 0;
    }
    case WM_TIMER:
    {
        int s = 0;
        if (wParam == TIMER_PLAY && g_isPlaying)
        {
            if (g_currentTimeMs > g_musicLengthMs)
                g_currentTimeMs = g_musicLengthMs;

            // ===== 时间显示（保留）=====
            wchar_t buf[32] = { 0 };
            int cur = g_currentTimeMs / 1000;
            int min = cur / 60;
            int sec = cur % 60;
            int total = g_musicLengthMs / 1000;
            int totalMin = total / 60;
            int totalSec = total % 60;
            swprintf_s(buf, _countof(buf), L"%02d:%02d / %02d:%02d", min, sec, totalMin, totalSec);
            if (hTime) SetWindowText(hTime, buf);

            // 刷新波形（保留）
            InvalidateRect(g_hWaveView, NULL, FALSE);

            // ===== 关键修改：只有用户未拖动滑块时，才更新TrackBar =====
            if (hTrack && !g_isDraggingTrackBar)
            {
                SendMessage(hTrack, TBM_SETRANGE, TRUE, MAKELONG(0, g_musicLengthMs / 1000));
                SendMessage(hTrack, TBM_SETPAGESIZE, 0, 1);
                SendMessage(hTrack, TBM_SETTICFREQ, 1, 0);
                SendMessage(hTrack, TBM_SETPOS, TRUE, g_currentTimeMs / 1000);
            }
        }
        return 0;
    }
    break;

    // ===== 修改 WM_HSCROLL 消息处理（核心：标记拖动状态）=====
    case WM_HSCROLL:
    {
        if ((HWND)lParam == hTrack && hTrack)
        {
            int scrollCode = LOWORD(wParam);

            switch (scrollCode)
            {
            case TB_THUMBTRACK:      // 用户正在拖动滑块（实时）
                // 设置标记：用户正在拖动，暂停定时器更新TrackBar
                g_isDraggingTrackBar = true;
                // 【可选】暂停播放，避免拖动时音频还在走
                // PausePlay();
                // g_isPlaying = false;
                [[fallthrough]]; // 继续执行下方逻辑（获取位置并更新）

            case TB_THUMBPOSITION:   // 用户松开滑块（结束）
            {
                int newPosSec = (int)SendMessage(hTrack, TBM_GETPOS, 0, 0);
                int maxSec = g_musicLengthMs / 1000;
                if (newPosSec < 0) newPosSec = 0;
                if (newPosSec > maxSec) newPosSec = maxSec;
                int newPosMs = newPosSec * 1000;

                // 调用Seek函数跳转音频
                SeekPlay((double)newPosSec, g_wav.nSamplesPerSec, g_wav.nChannels, g_wav.wBitsPerSample);
                g_currentTimeMs = newPosMs;

                // 更新时间显示
                wchar_t buf[32] = { 0 };
                int cur = newPosMs / 1000;
                int min = cur / 60;
                int sec = cur % 60;
                int total = g_musicLengthMs / 1000;
                int totalMin = total / 60;
                int totalSec = total % 60;
                swprintf_s(buf, _countof(buf), L"%02d:%02d / %02d:%02d", min, sec, totalMin, totalSec);
                if (hTime) SetWindowText(hTime, buf);

                InvalidateRect(g_hWaveView, NULL, FALSE);

                // 如果是拖动结束，恢复标记（允许定时器更新）
                if (scrollCode == TB_THUMBPOSITION)
                {
                    g_isDraggingTrackBar = false;
                    // 【可选】恢复播放（如果之前暂停了）
                    // if (g_isPlaying) StartPlay(extractFileName.c_str());
                }
                break;
            }

            // 其他操作（点击箭头/空白区）：不标记拖动，但正常处理位置
            case TB_LINEUP:
            case TB_LINEDOWN:
            case TB_PAGEUP:
            case TB_PAGEDOWN:
            {
                int newPosSec = (int)SendMessage(hTrack, TBM_GETPOS, 0, 0);
                int maxSec = g_musicLengthMs / 1000;
                if (newPosSec < 0) newPosSec = 0;
                if (newPosSec > maxSec) newPosSec = maxSec;
                int newPosMs = newPosSec * 1000;

                SeekPlay((double)newPosSec, g_wav.nSamplesPerSec, g_wav.nChannels, g_wav.wBitsPerSample);
                g_currentTimeMs = newPosMs;

                // 更新时间显示
                wchar_t buf[32] = { 0 };
                int cur = newPosMs / 1000;
                int min = cur / 60;
                int sec = cur % 60;
                int total = g_musicLengthMs / 1000;
                int totalMin = total / 60;
                int totalSec = total % 60;
                swprintf_s(buf, _countof(buf), L"%02d:%02d / %02d:%02d", min, sec, totalMin, totalSec);
                if (hTime) SetWindowText(hTime, buf);

                InvalidateRect(g_hWaveView, NULL, FALSE);
                break;
            }
            }
            return 0;
        }
        break;
    }
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
            g_isPlaying = false; // 补充暂停状态更新
        }
        break;
        case ID_MUSIC_STOP:
        {
            StopPlay();
            g_isPlaying = false; // 补充停止状态更新
            g_currentTimeMs = 0; // 重置播放时间
            // 重置进度条
            if (hTrack)
                SendMessage(hTrack, TBM_SETPOS, TRUE, 0);
            // 刷新时间显示
            if (hTime)
            {
                wchar_t buf[32] = { 0 };
                swprintf_s(buf, _countof(buf), L"00:00 / %02d:%02d", g_musicLengthMs / 60000, (g_musicLengthMs / 1000) % 60);
                SetWindowText(hTime, buf);
            }
        }
        break;
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

        int topBarH = toptool_height;
        int bottomBarH = toptool_height;
        int contentY = topBarH;
        int contentH = cy - topBarH - bottomBarH;

        if (contentH < 0) contentH = 0;

        // 使用 DeferWindowPos + SWP_NOCOPYBITS 防止旧内容复制
        HDWP hdwp = BeginDeferWindowPos(4);

        if (HWND hTop = GetDlgItem(hWnd, IDC_TOOLBAR_TOP))
            hdwp = DeferWindowPos(hdwp, hTop, nullptr,
                0, 0, cx, topBarH,
                SWP_NOZORDER | SWP_NOCOPYBITS);

        if (HWND hBottom = GetDlgItem(hWnd, IDC_TOOLBAR_BOTTOM))
            hdwp = DeferWindowPos(hdwp, hBottom, nullptr,
                0, cy - bottomBarH, cx, bottomBarH,
                SWP_NOZORDER | SWP_NOCOPYBITS);

        if (g_hTrackList)
            hdwp = DeferWindowPos(hdwp, g_hTrackList, nullptr,
                0, contentY, LEFT_PANEL_WIDTH, contentH,
                SWP_NOZORDER | SWP_NOCOPYBITS);

        if (g_hWaveView)
            hdwp = DeferWindowPos(hdwp, g_hWaveView, nullptr,
                LEFT_PANEL_WIDTH, contentY, cx - LEFT_PANEL_WIDTH, contentH,
                SWP_NOZORDER | SWP_NOCOPYBITS);

        EndDeferWindowPos(hdwp);

        // 强制所有子窗口重绘
        if (g_hTrackList) InvalidateRect(g_hTrackList, NULL, TRUE);
        if (g_hWaveView) InvalidateRect(g_hWaveView, NULL, TRUE);

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
        // 销毁定时器
        if (g_timerPlay)
            KillTimer(hWnd, g_timerPlay);

        // 清理GDI资源
        CleanupResources();

        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}