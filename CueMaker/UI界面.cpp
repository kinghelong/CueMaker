#pragma warning(disable : 4267) 
#include"framework.h"
#include<windowsx.h>
#include"resource.h"
extern HWND g_hWaveView ;
HWND hTime,hTrack;

// ===== 新增：双工具栏全局资源（分开管理，避免混淆）=====
extern HBITMAP g_hToolbarTopBmp ;      // 顶部工具栏位图
extern HIMAGELIST g_hImageListTop ;    // 顶部工具栏图片列表
extern HBITMAP g_hToolbarBottomBmp ;   // 底部工具栏位图
extern HIMAGELIST g_hImageListBottom ; // 底部工具栏图片列表

bool isDragging = false, g_isPlaying = false;
double g_zoomFactor = 1.0;    // 缩放倍率，1.0 是显示全长
double g_scrollOffset = 0.0;  // 当前画面左侧相对于起始点的偏移像素
int clickX = 0;
int g_currentTimeMs = 0;  // 当前播放时间毫秒
int g_musicLengthMs = 0;   // WAV总时长毫秒（LoadWavFile时设置）


// 窗口过程（原有函数，保留不变）
LRESULT CALLBACK WaveCtrlProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 注意：不再在这里定义 static isDragging，直接用你顶部的全局变量
    static int lastMouseX = 0;

    switch (message)
    {
    case WM_CREATE:
    {
       
    }
        break;
   


	case WM_SIZE:
    {
        RECT rc;
		GetClientRect(g_hWaveView, &rc);
		InvalidateRect(g_hWaveView, &rc, TRUE);

    }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        DrawDualChannelWaveform(hdc, hWnd); // 内部已经是双缓冲，这里很安全        
        EndPaint(hWnd, &ps);
    }
    return 0;
    case WM_LBUTTONDOWN:
    {
        clickX = GET_X_LPARAM(lParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        InvalidateRect(hWnd, &rc, TRUE);
    }
        break;
    case WM_RBUTTONDOWN:
        isDragging = true;
        lastMouseX = GET_X_LPARAM(lParam); // 记录起始点
        SetCapture(hWnd);
        return 0;

    case WM_RBUTTONUP:
        isDragging = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (isDragging) {
            int currentX = GET_X_LPARAM(lParam);
            int deltaX = currentX - lastMouseX;
            g_scrollOffset -= deltaX;

            RECT rc; GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            double zoomedTotalWidth = (double)width * g_zoomFactor;
            double maxOffset = zoomedTotalWidth - width;

            if (g_scrollOffset < 0) g_scrollOffset = 0;
            if (g_scrollOffset > maxOffset) g_scrollOffset = maxOffset;

            lastMouseX = currentX;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL:
    {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        double oldZoom = g_zoomFactor;

        if (zDelta > 0) g_zoomFactor *= 1.2;
        else g_zoomFactor /= 1.2;

        if (g_zoomFactor < 1.0) g_zoomFactor = 1.0;
        if (g_zoomFactor > 500.0) g_zoomFactor = 500.0;

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hWnd, &pt);

        // 以鼠标位置为中心的缩放补偿公式
        g_scrollOffset = (g_scrollOffset + pt.x) * (g_zoomFactor / oldZoom) - pt.x;

        RECT rc; GetClientRect(hWnd, &rc);
        double maxOffset = (rc.right * g_zoomFactor) - rc.right;
        if (g_scrollOffset < 0) g_scrollOffset = 0;
        if (g_scrollOffset > maxOffset) g_scrollOffset = maxOffset;

        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // 阻止系统擦除，完全由双缓冲控制背景，防止闪烁

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}


// 关于对话框
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
// ===== 修复后：创建顶部工具栏（保留3个按钮，加背景让按钮边界清晰）=====
HWND CreateToolbarTop(HWND hWnd, HINSTANCE hInstance)
{
    HWND hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP, 0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR_TOP, hInstance, NULL);
    // 带数字限制的 EDIT 控件创建（补充 ES_NUMBER 样式）
    HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT("0"), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT | ES_NUMBER, 638, 10, 50, 22, hToolbar, (HMENU)IDC_MUTE_EDIT, hInstance, NULL);
    // 设置字体（保持可见性）
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
//    HWND hEdit = CreateWindow(TEXT("EDIT"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 500, 10, 150, 22, hWnd, (HMENU)IDC_MUTE_EDIT, hInstance, NULL);
    HWND hVolumeSlider = CreateWindow(TRACKBAR_CLASS, TEXT(""), WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ, 720, 10, 100, 22, hWnd, (HMENU)IDC_VOLUME_SLIDER, hInstance, NULL);
    SendMessage(hVolumeSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessage(hVolumeSlider, TBM_SETPOS, TRUE, 80); // 默认音量80%

    HWND hStaticTip = CreateWindowEx(0, TEXT("STATIC"), TEXT("静音区阈值"), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY, 638, 10 + 22 + 5, 60, 16, hToolbar, (HMENU)IDC_STATIC_MUTE_TIP, hInstance, NULL);
    // 给 Static 设置和 EDIT 一致的字体，确保样式统一
    SendMessage(hStaticTip, WM_SETFONT, (WPARAM)hFont, TRUE);

    // （可选）设置 Static 背景透明，避免和工具栏背景冲突
    SetWindowLongPtr(hStaticTip, GWL_EXSTYLE, GetWindowLongPtr(hStaticTip, GWL_EXSTYLE) | WS_EX_TRANSPARENT);

    HWND hVolumTip = CreateWindowEx(0, TEXT("STATIC"), TEXT("音量"), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY, 720, 10 + 22 + 5, 50, 16, hToolbar, (HMENU)IDC_STATIC_VOLUM_TIP, hInstance, NULL);

    // 1. 创建 Toolbar
    
    if (hToolbar == NULL) return NULL;

    if (!hToolbar)
        return NULL;

    // ★ 必须调用，否则很多行为异常
    SendMessage(hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    // ------------------------------------------------------------
    // 2. 从资源加载位图（关键点）
    // ------------------------------------------------------------
    HBITMAP hBmp = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(IDB_TOPBAR_REMAKE), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

    if (!hBmp)
        return hToolbar;   // 至少 toolbar 还能显示

    const int BTN_SIZE = TOOLBAR_HEIGHT;
    const int BTN_COUNT = 11;

    // ------------------------------------------------------------
    // 3. 创建 ImageList，并加入位图
    // ------------------------------------------------------------
    HIMAGELIST hImg = ImageList_Create(BTN_SIZE, BTN_SIZE, ILC_COLOR32 | ILC_MASK, BTN_COUNT, 0);

    ImageList_Add(hImg, hBmp, NULL);
    DeleteObject(hBmp);

    // 绑定 ImageList
    SendMessage(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImg);

    // 设置尺寸
    SendMessage(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(BTN_SIZE, BTN_SIZE));
    SendMessage(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(BTN_SIZE + 4, BTN_SIZE + 4));

    // ------------------------------------------------------------
    // 4. 添加按钮
    // ------------------------------------------------------------
    TBBUTTON tbb[BTN_COUNT] = {};

    for (int i = 0; i < BTN_COUNT; i++)
    {
        tbb[i].iBitmap = i;                        // ImageList 索引
        tbb[i].idCommand = ID_TB_NEW_CUE + i;     // 你的命令 ID
        tbb[i].fsState = TBSTATE_ENABLED;
        tbb[i].fsStyle = TBSTYLE_BUTTON;
        tbb[i].dwData = 0;
        tbb[i].iString = -1;
    }

    SendMessage(hToolbar, TB_ADDBUTTONS, BTN_COUNT, (LPARAM)tbb);
    SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);

    // ------------------------------------------------------------
    // 5. 可选：设置背景色（不强制）
    // ------------------------------------------------------------
    SetClassLongPtr(hToolbar, GCLP_HBRBACKGROUND, (LONG_PTR)GetStockObject(LTGRAY_BRUSH));

    InvalidateRect(hToolbar, NULL, TRUE);

    return hToolbar;
}

#define BOTTOM_BTN_COUNT 7
#define SEP_TRACKBAR_WIDTH 320
#define SEP_TIME_WIDTH     180

HWND CreateToolbarBottom(HWND hWnd, HINSTANCE hInstance)
{
    HWND hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_BOTTOM, 0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR_BOTTOM, hInstance, NULL);
    if (!hToolbar)
        return NULL;

    SendMessage(hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    // ===== 图像列表（48x48，不动）=====
    HBITMAP hBmp = (HBITMAP)LoadImage(hInstance, MAKEINTRESOURCE(IDB_PLAYBAR), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    const int BTN_SIZE = 48;

    HIMAGELIST hImg = ImageList_Create(BTN_SIZE, BTN_SIZE, ILC_COLOR32 | ILC_MASK, BOTTOM_BTN_COUNT, 0);

    ImageList_Add(hImg, hBmp, NULL);
    DeleteObject(hBmp);

    SendMessage(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImg);
    SendMessage(hToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(BTN_SIZE, BTN_SIZE));
    SendMessage(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(BTN_SIZE, BTN_SIZE));

    // ===== 按钮 + SEP =====
    TBBUTTON tbb[BOTTOM_BTN_COUNT + 2] = {};

    // 7 个播放按钮
    for (int i = 0; i < BOTTOM_BTN_COUNT; ++i)
    {
        tbb[i].iBitmap = i;
        tbb[i].idCommand = ID_MUSIC_PRE + i;
        tbb[i].fsState = TBSTATE_ENABLED;
        tbb[i].fsStyle = TBSTYLE_BUTTON;
    }

    // Trackbar 占位
    int IDX_TRACKBAR = BOTTOM_BTN_COUNT;
    tbb[IDX_TRACKBAR].fsStyle = TBSTYLE_SEP;
    tbb[IDX_TRACKBAR].iBitmap = SEP_TRACKBAR_WIDTH;

    // 时间显示占位
    int IDX_TIME = BOTTOM_BTN_COUNT + 1;
    tbb[IDX_TIME].fsStyle = TBSTYLE_SEP;
    tbb[IDX_TIME].iBitmap = SEP_TIME_WIDTH;

    SendMessage(hToolbar, TB_ADDBUTTONS, ARRAYSIZE(tbb), (LPARAM)tbb);
    SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);

    // ===== Trackbar =====
    RECT rcTrack;
    SendMessage(hToolbar, TB_GETITEMRECT, IDX_TRACKBAR, (LPARAM)&rcTrack);

    hTrack = CreateWindowEx(0, TRACKBAR_CLASS, TEXT(""), WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, rcTrack.left, rcTrack.top + (rcTrack.bottom - rcTrack.top - 24) / 2, rcTrack.right - rcTrack.left, 24, hToolbar, (HMENU)IDC_PROGRESS, hInstance, NULL);

    SendMessage(hTrack, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
    SendMessage(hTrack, TBM_SETPOS, TRUE, 0);

    // ===== 时间显示 =====
    RECT rcTime;
    SendMessage(hToolbar, TB_GETITEMRECT, IDX_TIME, (LPARAM)&rcTime);

    hTime = CreateWindowEx(0, TEXT("STATIC"), TEXT("00:00 / 00:00"), WS_CHILD | WS_VISIBLE | SS_CENTER, rcTime.left, rcTime.top + (rcTime.bottom - rcTime.top - 32) / 2, rcTime.right - rcTime.left, 32, hToolbar, (HMENU)IDC_TIME_DISPLAY, hInstance, NULL);

    HFONT hFont = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Segoe UI"));

    SendMessage(hTime, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetWindowLongPtr(hTime, GWLP_USERDATA, (LONG_PTR)hFont);

    // 背景
    SetClassLongPtr(hToolbar, GCLP_HBRBACKGROUND,(LONG_PTR)GetStockObject(LTGRAY_BRUSH));

    InvalidateRect(hToolbar, NULL, TRUE);

    DeleteObject(hFont);
    return hToolbar;
}
