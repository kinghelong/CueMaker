#pragma warning(disable : 4267) 
#include "framework.h"
#include <windowsx.h>
#include "resource.h"

// ===================================================================
// 外部声明
// ===================================================================
extern HWND g_hWaveView;
extern void SeekPlay(double seconds, int sampleRate, int channels, int bitsPerSample);
extern WAVEFORMATEX g_wav;

// ===================================================================
// 全局变量
// ===================================================================

// 工具栏相关
extern HBITMAP g_hToolbarTopBmp;
extern HIMAGELIST g_hImageListTop;
extern HBITMAP g_hToolbarBottomBmp;
extern HIMAGELIST g_hImageListBottom;
HIMAGELIST g_hImgSwitch = NULL;


// UI 控件句柄
HWND hTime = NULL;      // 时间显示控件
HWND hTrack = NULL;     // 进度条控件

// 播放状态
bool g_isPlaying = false;
int g_currentTimeMs = 0;    // 当前播放位置（毫秒）
int g_musicLengthMs = 0;    // 音频总时长（毫秒）
double g_clickTimeRatio;

// 波形控件状态
bool g_isWaveDragging = false;
int clickX = 0;
double g_zoomFactor = 1.0;
double g_scrollOffset = 0.0;

// TrackBar 子类化
WNDPROC g_pOldTrackBarProc = nullptr;

// ===================================================================
// TrackBar 子类化窗口过程
// ===================================================================
LRESULT CALLBACK TrackBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    {
        // 直接调用原始过程处理鼠标事件
        LRESULT result = CallWindowProc(g_pOldTrackBarProc, hWnd, uMsg, wParam, lParam);

        // 获取当前位置并同步
        int newPosSec = (int)SendMessage(hWnd, TBM_GETPOS, 0, 0);
        int maxSec = g_musicLengthMs / 1000;

        if (newPosSec < 0) newPosSec = 0;
        if (newPosSec > maxSec) newPosSec = maxSec;

        int newPosMs = newPosSec * 1000;

        // 鼠标松开时才真正 Seek
        if (uMsg == WM_LBUTTONUP)
        {
            if (g_wav.nSamplesPerSec > 0)
            {
                SeekPlay((double)newPosSec, g_wav.nSamplesPerSec, g_wav.nChannels, g_wav.wBitsPerSample);
            }
            g_currentTimeMs = newPosMs;
        }

        // 更新时间显示
        if (hTime)
        {
            int cur = newPosMs / 1000;
            int min = cur / 60;
            int sec = cur % 60;
            int total = g_musicLengthMs / 1000;
            int totalMin = total / 60;
            int totalSec = total % 60;

            wchar_t buf[32];
            swprintf_s(buf, L"%02d:%02d / %02d:%02d", min, sec, totalMin, totalSec);
            SetWindowText(hTime, buf);
        }

        // 刷新波形
        if (g_hWaveView)
        {
            InvalidateRect(g_hWaveView, NULL, FALSE);
        }

        return result;
    }

    case WM_KEYDOWN:
    {
        // 处理键盘控制
        LRESULT result = CallWindowProc(g_pOldTrackBarProc, hWnd, uMsg, wParam, lParam);

        int newPosSec = (int)SendMessage(hWnd, TBM_GETPOS, 0, 0);
        int maxSec = g_musicLengthMs / 1000;

        if (newPosSec < 0) newPosSec = 0;
        if (newPosSec > maxSec) newPosSec = maxSec;

        if (g_wav.nSamplesPerSec > 0)
        {
            SeekPlay((double)newPosSec, g_wav.nSamplesPerSec, g_wav.nChannels, g_wav.wBitsPerSample);
        }

        g_currentTimeMs = newPosSec * 1000;

        if (hTime)
        {
            int cur = newPosSec;
            int min = cur / 60;
            int sec = cur % 60;
            int total = g_musicLengthMs / 1000;
            int totalMin = total / 60;
            int totalSec = total % 60;

            wchar_t buf[32];
            swprintf_s(buf, L"%02d:%02d / %02d:%02d", min, sec, totalMin, totalSec);
            SetWindowText(hTime, buf);
        }

        if (g_hWaveView)
        {
            InvalidateRect(g_hWaveView, NULL, FALSE);
        }

        return result;
    }

    default:
        return CallWindowProc(g_pOldTrackBarProc, hWnd, uMsg, wParam, lParam);
    }
}

// ===================================================================
// 波形控件窗口过程
// ===================================================================
LRESULT CALLBACK WaveCtrlProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int lastMouseX = 0;

    switch (message)
    {
    case WM_CREATE:
        return 0;

    case WM_ERASEBKGND:
        // 返回1，在 WM_PAINT 中自己绘制背景
        return 1;

    case WM_SIZE:
    {
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        if (width > 0 && height > 0)
        {
            // 双缓冲绘图
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            // 先填充黑色背景
            HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(memDC, &rc, blackBrush);

            // 绘制波形
            DrawDualChannelWaveform(memDC, hWnd);

            // 复制到屏幕
            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

            // 清理
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int mouseX = GET_X_LPARAM(lParam);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right - rc.left;

        if (width <= 0 || !g_isPlaying)
        {
            g_clickTimeRatio = -1.0;
            clickX = -1;
            return 0;
        }

        // ===== 关键修复：计算点击位置对应的实际时间比例 =====
        // 公式：(鼠标X + 滚动偏移) / 缩放后总宽度
        double zoomedTotalWidth = width * g_zoomFactor;
        g_clickTimeRatio = (mouseX + g_scrollOffset) / zoomedTotalWidth;

        // 限制范围 [0.0, 1.0]
        if (g_clickTimeRatio < 0.0) g_clickTimeRatio = 0.0;
        if (g_clickTimeRatio > 1.0) g_clickTimeRatio = 1.0;

        // 更新全局 clickX（用于兼容旧代码，如果不需要可以删除）
        clickX = mouseX;

        // 计算实际时间（毫秒）并更新播放位置
        g_currentTimeMs = (int)(g_clickTimeRatio * g_musicLengthMs);

        // 如果正在播放，执行 Seek
        if (g_isPlaying && g_wav.nSamplesPerSec > 0)
        {
            double seekSeconds = g_currentTimeMs / 1000.0;
            SeekPlay(seekSeconds,
                g_wav.nSamplesPerSec,
                g_wav.nChannels,
                g_wav.wBitsPerSample);
        }

        // 刷新显示
        InvalidateRect(hWnd, NULL, FALSE);

        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        g_isWaveDragging = true;
        lastMouseX = GET_X_LPARAM(lParam);
        SetCapture(hWnd);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        g_isWaveDragging = false;
        ReleaseCapture();
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (g_isWaveDragging)
        {
            int currentX = GET_X_LPARAM(lParam);
            int deltaX = currentX - lastMouseX;
            g_scrollOffset -= deltaX;

            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            double zoomedTotalWidth = (double)width * g_zoomFactor;
            double maxOffset = zoomedTotalWidth - width;

            if (g_scrollOffset < 0)
                g_scrollOffset = 0;
            if (g_scrollOffset > maxOffset)
                g_scrollOffset = maxOffset;

            lastMouseX = currentX;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        double oldZoom = g_zoomFactor;

        // 缩放
        if (zDelta > 0)
            g_zoomFactor *= 1.2;
        else
            g_zoomFactor /= 1.2;

        // 限制范围
        if (g_zoomFactor < 1.0) g_zoomFactor = 1.0;
        if (g_zoomFactor > 500.0) g_zoomFactor = 500.0;

        // 计算鼠标位置
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hWnd, &pt);

        // 以鼠标为中心缩放
        g_scrollOffset = (g_scrollOffset + pt.x) * (g_zoomFactor / oldZoom) - pt.x;

        // 限制滚动范围
        RECT rc;
        GetClientRect(hWnd, &rc);
        double maxOffset = (rc.right * g_zoomFactor) - rc.right;
        if (g_scrollOffset < 0) g_scrollOffset = 0;
        if (g_scrollOffset > maxOffset) g_scrollOffset = maxOffset;

        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}

// ===================================================================
// 关于对话框
// ===================================================================
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

// ===================================================================
// 创建顶部工具栏
// ===================================================================
HWND CreateToolbarTop(HWND hWnd, HINSTANCE hInstance)
{
    // 创建工具栏
    HWND hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP, 0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR_TOP, hInstance, NULL);

    if (!hToolbar) return NULL;

    // 设置字体
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // 创建静音阈值编辑框
    HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT | ES_NUMBER, 638, 10, 50, 22, hToolbar, (HMENU)IDC_MUTE_EDIT, hInstance, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 创建音量滑块
    HWND hVolumeSlider = CreateWindow(TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ, 720, 10, 100, 22, hToolbar, (HMENU)IDC_VOLUME_SLIDER, hInstance, NULL);
    SendMessage(hVolumeSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessage(hVolumeSlider, TBM_SETPOS, TRUE, 80);

    // 创建静态文本（静音阈值提示）
    HWND hStaticMute = CreateWindowEx(0, L"STATIC", L"静音区阈值", WS_CHILD | WS_VISIBLE | SS_LEFT, 638, 37, 60, 16, hToolbar, (HMENU)IDC_STATIC_MUTE_TIP, hInstance, NULL);
    SendMessage(hStaticMute, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 创建静态文本（音量提示）
    HWND hStaticVolume = CreateWindowEx(0, L"STATIC", L"音量", WS_CHILD | WS_VISIBLE | SS_LEFT, 720, 37, 50, 16, hToolbar, (HMENU)IDC_STATIC_VOLUM_TIP, hInstance, NULL);
    SendMessage(hStaticVolume, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 初始化工具栏
    SendMessage(hToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    // 加载位图
    HBITMAP hBmp = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(IDB_TOPBAR_REMAKE), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!hBmp) return hToolbar;

    const int BTN_SIZE = TOOLBAR_HEIGHT;
    const int BTN_COUNT = 11;

    // 创建图像列表
    HIMAGELIST hImg = ImageList_Create(BTN_SIZE, BTN_SIZE, ILC_COLOR32 | ILC_MASK, BTN_COUNT, 0);
    ImageList_Add(hImg, hBmp, NULL);
    DeleteObject(hBmp);

    // 设置工具栏图像
    SendMessage(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImg);
    SendMessage(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(BTN_SIZE, BTN_SIZE));
    SendMessage(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(BTN_SIZE + 4, BTN_SIZE + 4));

    // 添加按钮
    TBBUTTON tbb[BTN_COUNT];
    for (int i = 0; i < BTN_COUNT; i++)
    {
        tbb[i].iBitmap = i;
        tbb[i].idCommand = ID_TB_NEW_CUE + i;
        tbb[i].fsState = TBSTATE_ENABLED;
        tbb[i].fsStyle = TBSTYLE_BUTTON;
        tbb[i].dwData = 0;
        tbb[i].iString = -1;
    }

    SendMessage(hToolbar, TB_ADDBUTTONS, (WPARAM)BTN_COUNT, (LPARAM)&tbb);
    SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);

    return hToolbar;
}

// ===================================================================
// 创建底部工具栏
// ===================================================================
#define BOTTOM_BTN_COUNT 7
#define SEP_TRACKBAR_WIDTH 620
#define SEP_TIME_WIDTH 180

HWND CreateToolbarBottom(HWND hWnd, HINSTANCE hInstance)
{
    // 创建工具栏
    HWND hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_BOTTOM, 0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR_BOTTOM, hInstance, NULL);

    if (!hToolbar) return NULL;

    // 初始化工具栏
    SendMessage(hToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    // 加载播放按钮位图
    HBITMAP hBmp = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(IDB_PLAYBAR), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    if (!hBmp) return hToolbar;

    const int BTN_SIZE = 48;

    // 创建图像列表
    HIMAGELIST hImg = ImageList_Create(BTN_SIZE, BTN_SIZE, ILC_COLOR32 | ILC_MASK, BOTTOM_BTN_COUNT, 0);
    ImageList_Add(hImg, hBmp, NULL);
    DeleteObject(hBmp);

    // 设置工具栏图像
    SendMessage(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImg);
    SendMessage(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(BTN_SIZE, BTN_SIZE));
    SendMessage(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(BTN_SIZE, BTN_SIZE));

    // 定义按钮和分隔符
    const int TOTAL_ITEMS = BOTTOM_BTN_COUNT + 2;
    TBBUTTON tbb[TOTAL_ITEMS] = {};

    // 播放控制按钮
    for (int i = 0; i < BOTTOM_BTN_COUNT; i++)
    {
        tbb[i].iBitmap = i;
        tbb[i].idCommand = ID_MUSIC_PRE + i;
        tbb[i].fsState = TBSTATE_ENABLED;
        tbb[i].fsStyle = TBSTYLE_BUTTON;
    }

    // TrackBar 分隔符
    int IDX_TRACKBAR = BOTTOM_BTN_COUNT;
    tbb[IDX_TRACKBAR].fsStyle = TBSTYLE_SEP;
    tbb[IDX_TRACKBAR].iBitmap = SEP_TRACKBAR_WIDTH;

    // 时间显示分隔符
    int IDX_TIME = BOTTOM_BTN_COUNT + 1;
    tbb[IDX_TIME].fsStyle = TBSTYLE_SEP;
    tbb[IDX_TIME].iBitmap = SEP_TIME_WIDTH;

    // 添加所有项
    SendMessage(hToolbar, TB_ADDBUTTONS, (WPARAM)TOTAL_ITEMS, (LPARAM)&tbb);
    SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);

    // ===== 创建 TrackBar =====
    RECT rcTrack;
    if (SendMessage(hToolbar, TB_GETITEMRECT, IDX_TRACKBAR, (LPARAM)&rcTrack))
    {
        int trackHeight = 24;
        int trackY = rcTrack.top + (rcTrack.bottom - rcTrack.top - trackHeight) / 2;

        hTrack = CreateWindowEx(0, TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, rcTrack.left, trackY, rcTrack.right - rcTrack.left, trackHeight, hToolbar, (HMENU)IDC_PROGRESS, hInstance, NULL);

        if (hTrack)
        {
            // 初始化范围（秒）
            SendMessage(hTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessage(hTrack, TBM_SETPOS, TRUE, 0);
            SendMessage(hTrack, TBM_SETLINESIZE, 0, 1);    // 箭头键步进1秒
            SendMessage(hTrack, TBM_SETPAGESIZE, 0, 10);   // PageUp/Down步进10秒

            // 子类化 TrackBar
            g_pOldTrackBarProc = (WNDPROC)SetWindowLongPtr(
                hTrack,
                GWLP_WNDPROC,
                (LONG_PTR)TrackBarSubclassProc
            );
        }
    }

    // ===== 创建时间显示 =====
    RECT rcTime;
    if (SendMessage(hToolbar, TB_GETITEMRECT, IDX_TIME, (LPARAM)&rcTime))
    {
        int timeHeight = 32;
        int timeY = rcTime.top + (rcTime.bottom - rcTime.top - timeHeight) / 2;

        hTime = CreateWindowEx(0, L"STATIC", L"00:00 / 00:00", WS_CHILD | WS_VISIBLE | SS_CENTER, rcTime.left, timeY, rcTime.right - rcTime.left, timeHeight, hToolbar, (HMENU)IDC_TIME_DISPLAY, hInstance, NULL);

        if (hTime)
        {
            // 创建字体
            HFONT hFont = CreateFont(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            if (hFont)
            {
                SendMessage(hTime, WM_SETFONT, (WPARAM)hFont, TRUE);
                // 保存字体句柄到窗口用户数据，方便清理
                SetWindowLongPtr(hTime, GWLP_USERDATA, (LONG_PTR)hFont);
            }
        }
    }

    return hToolbar;
}

// ===================================================================
// 资源清理
// ===================================================================
void CleanupToolbarResources()
{
    // 清理时间显示控件的字体
    if (hTime)
    {
        HFONT hFont = (HFONT)GetWindowLongPtr(hTime, GWLP_USERDATA);
        if (hFont)
        {
            DeleteObject(hFont);
            SetWindowLongPtr(hTime, GWLP_USERDATA, 0);
        }
    }

    // 还原 TrackBar 窗口过程
    if (hTrack && g_pOldTrackBarProc)
    {
        SetWindowLongPtr(hTrack, GWLP_WNDPROC, (LONG_PTR)g_pOldTrackBarProc);
        g_pOldTrackBarProc = nullptr;
    }
}

// 初始化切换用的ImageList（程序启动时调用，在CreateToolbarBottom之后）
void InitSwitchImageList(HINSTANCE hInstance)
{
    const int BTN_SIZE = 48;
    // 加载切换用的位图（播放按钮变灰/暂停的版本）
    HBITMAP hBmpSwitch = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(IDB_PLAY_SWITCH), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (hBmpSwitch)
    {
        g_hImgSwitch = ImageList_Create(BTN_SIZE, BTN_SIZE, ILC_COLOR32 | ILC_MASK, BOTTOM_BTN_COUNT, 0);
        ImageList_Add(g_hImgSwitch, hBmpSwitch, NULL);
        DeleteObject(hBmpSwitch);
    }
}

// 改工具栏单个按钮的图标（ImageList模式专用）
// hToolbar：工具栏句柄
// nCmdID：按钮ID（比如ID_MUSIC_PLAY）
// nNewBmpIdx：新的ImageList索引
void ChangeToolBarBtnIcon(HWND hToolbar, UINT nCmdID, int nNewBmpIdx)
{
    if (!hToolbar || !IsWindow(hToolbar)) return;

    TBBUTTONINFO tbbi = { 0 };
    tbbi.cbSize = sizeof(TBBUTTONINFO);
    tbbi.dwMask = TBIF_IMAGE; // 只改图标索引
    tbbi.iImage = nNewBmpIdx; // 新的ImageList索引

    // 关键API：修改单个按钮的信息（只改图标，不影响其他）
    SendMessage(hToolbar, TB_SETBUTTONINFO, (WPARAM)nCmdID, (LPARAM)&tbbi);
}