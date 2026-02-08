#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include "resource.h"
#include <StrSafe.h>
#include"framework.h"

// 右键菜单ID（用于粘贴功能）
#define IDM_PASTE                   2000  // 粘贴菜单项

// 曲目信息结构体（扩展为4列对应字段）
typedef struct {
    int index;          // 第1列：ID（序号）
    TCHAR title[256];   // 第2列：歌曲名
    TCHAR album[256];   // 第3列：专辑名
    TCHAR artist[256];  // 第4列：作者名
} FullTrackInfo;

// 全局/静态变量声明
static HWND hTrackList;       // 曲目ListView句柄
static const int MAX_TRACK_NUM = 200; // 最大曲目数限制
static FullTrackInfo g_TrackArr[MAX_TRACK_NUM];  // 数组大小与MAX_TRACK_NUM匹配
static int g_TrackCount = 0;   // 当前曲目总数
static HMENU hPopupMenu;      // 右键弹出菜单句柄
extern HWND g_hTrackList ;

// 辅助：获取编辑框句柄（简化代码，避免重复GetDlgItem）
HWND GetAlbumEdit(HWND hDlg) { return GetDlgItem(hDlg, IDC_ALBUM_NAME); }
HWND GetArtistEdit(HWND hDlg) { return GetDlgItem(hDlg, IDC_ALBUM_ARTIST); }
HWND GetTrackTitleEdit(HWND hDlg) { return GetDlgItem(hDlg, IDC_TRACK_TITLE); }

// 辅助函数：交换ListView中两项内容（支持曲目上下移动，适配4列）
void ListView_SwapItems(HWND hListView, int idx1, int idx2)
{
    if (idx1 < 0 || idx2 < 0 || idx1 == idx2 || hListView == NULL) return;

    // 临时存储4列数据
    TCHAR szText1[4][256] = { 0 };
    TCHAR szText2[4][256] = { 0 };
  
    // 获取两项的4列数据
    for (int i = 0; i < 4; i++) {
        ListView_GetItemText(hListView, idx1, i, szText1[i], 256);
        ListView_GetItemText(hListView, idx2, i, szText2[i], 256);
    }

    // 交换4列显示内容
    for (int i = 0; i < 4; i++) {
        ListView_SetItemText(hListView, idx1, i, szText2[i]);
        ListView_SetItemText(hListView, idx2, i, szText1[i]);
    }
}

// 辅助函数：获取程序运行目录（解决INI文件路径不明确问题）
void GetExeDirectory(TCHAR* szDir, DWORD dwSize) {
    GetModuleFileName(NULL, szDir, dwSize);
    TCHAR* pLastSlash = _tcsrchr(szDir, _T('\\'));
    if (pLastSlash != NULL) {
        *(pLastSlash + 1) = _T('\0');
    }
}

// 辅助函数：从剪贴板获取文本
BOOL GetClipboardText(TCHAR* szText, DWORD dwSize) {
    if (szText == NULL || dwSize == 0) return FALSE;
    ZeroMemory(szText, dwSize * sizeof(TCHAR));

    if (!OpenClipboard(NULL)) {
        return FALSE;
    }
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        CloseClipboard();
        return FALSE;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL) {
        CloseClipboard();
        return FALSE;
    }

    TCHAR* pClipText = (TCHAR*)GlobalLock(hData);
    if (pClipText == NULL) {
        CloseClipboard();
        return FALSE;
    }

    HRESULT hr = StringCchCopyN(szText, dwSize, pClipText, dwSize - 1);
    GlobalUnlock(hData);
    CloseClipboard();
    return (hr == S_OK) && (_tcslen(szText) > 0);
}

// 辅助函数：初始化ListView（扩展为4列，ID、歌曲名、专辑名、作者名）
void InitTrackListView(HWND hListView) {
    if (hListView == NULL) return;
    // 修复：添加 LVS_REPORT 样式，确保列头显示
    DWORD dwStyle = GetWindowLong(hListView, GWL_STYLE);
    dwStyle = (dwStyle & ~(LVS_LIST | LVS_SMALLICON | LVS_ICON)) | LVS_REPORT;
    SetWindowLong(hListView, GWL_STYLE, dwStyle);

    // 设置ListView扩展样式
    ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // 定义4列的配置（列名、宽度）
    struct {
        TCHAR* szName;
        int cx;
    } columns[4] = {
        {(TCHAR * )_T("ID"), 10},
        {(TCHAR*)_T("歌曲名"), 200},
        {(TCHAR*)_T("专辑名"), 150},
        {(TCHAR*)_T("作者名"), 150}
    };

    // 添加4列到ListView
    LVCOLUMN lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int i = 0; i < 4; i++) {
        lvc.pszText = columns[i].szName;
        lvc.cx = columns[i].cx;
        ListView_InsertColumn(hListView, i, &lvc);
    }
}

// 辅助函数：按行分割字符串
int SplitTextByLine(const TCHAR* szSrc, TCHAR szLineArr[][256], int maxLine, int lineBufSize) {
    if (szSrc == NULL || szLineArr == NULL || maxLine <= 0 || lineBufSize <= 0)
        return 0;

    int lineCount = 0;
    const TCHAR* pStart = szSrc;
    const TCHAR* pCur = szSrc;

    for (int i = 0; i < maxLine; i++) {
        ZeroMemory(szLineArr[i], lineBufSize * sizeof(TCHAR));
    }

    while (*pCur != _T('\0') && lineCount < maxLine) {
        if (*pCur == _T('\n') || *pCur == _T('\r')) {
            int len = (int)(pCur - pStart);
            if (len > 0 && len < lineBufSize) {
                StringCchCopyN(szLineArr[lineCount], lineBufSize, pStart, len);
                lineCount++;
            }
            if (*pCur == _T('\r') && *(pCur + 1) == _T('\n')) {
                pCur++;
            }
            pStart = pCur + 1;
        }
        pCur++;
    }

    if (pStart < pCur && lineCount < maxLine) {
        int len = (int)(pCur - pStart);
        if (len > 0 && len < lineBufSize) {
            StringCchCopyN(szLineArr[lineCount], lineBufSize, pStart, len);
            lineCount++;
        }
    }

    return lineCount;
}

// 辅助函数：批量更新ListView所有行的专辑名/作者名（核心需求：修改后全局同步）
void UpdateAllTrackAlbumAndArtist(HWND hListView, const TCHAR* szAlbum, const TCHAR* szArtist) {
    if (hListView == NULL || szAlbum == NULL || szArtist == NULL) return;

    // 1. 更新ListView所有行的第3列（专辑名）、第4列（作者名）
    for (int i = 0; i < g_TrackCount; i++) {
        ListView_SetItemText(hListView, i, 2, (LPWSTR)szAlbum);
        ListView_SetItemText(hListView, i, 3, (LPWSTR)szArtist);

        // 2. 同步更新数组数据（保证数据一致性）
        StringCchCopy(g_TrackArr[i].album, 256, szAlbum);
        StringCchCopy(g_TrackArr[i].artist, 256, szArtist);
    }
}

// 辅助函数：添加单条曲目到ListView和数组（适配4列，自动生成ID）
BOOL AddSingleTrack(HWND hDlg, const TCHAR* szTitle) {
    if (szTitle == NULL || hDlg == NULL || hTrackList == NULL) return FALSE;
    if (g_TrackCount >= MAX_TRACK_NUM || _tcslen(szTitle) == 0) return FALSE;

    // 获取当前编辑框中的专辑名、作者名（全局统一）
    TCHAR szAlbum[256] = { 0 };
    TCHAR szArtist[256] = { 0 };
    GetDlgItemText(hDlg, IDC_ALBUM_NAME, szAlbum, 256);
    GetDlgItemText(hDlg, IDC_ALBUM_ARTIST, szArtist, 256);

    // 赋值到曲目数组（4列数据）
    g_TrackArr[g_TrackCount].index = g_TrackCount + 1;
    StringCchCopy(g_TrackArr[g_TrackCount].title, 256, szTitle);
    StringCchCopy(g_TrackArr[g_TrackCount].album, 256, szAlbum);
    StringCchCopy(g_TrackArr[g_TrackCount].artist, 256, szArtist);

    // 添加到ListView（4列数据）
    TCHAR szID[32] = { 0 };
    StringCchPrintf(szID, 32, _T("%d"), g_TrackArr[g_TrackCount].index);

    LVITEM lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = g_TrackCount;
    lvi.iSubItem = 0;
    lvi.pszText = szID;
    ListView_InsertItem(hTrackList, &lvi);

    // 设置第2-4列内容
    ListView_SetItemText(hTrackList, g_TrackCount, 1, (LPWSTR)szTitle);
    ListView_SetItemText(hTrackList, g_TrackCount, 2, (LPWSTR)szAlbum);
    ListView_SetItemText(hTrackList, g_TrackCount, 3, (LPWSTR)szArtist);

    // 更新曲目总数
    g_TrackCount++;
    return TRUE;
}

// 辅助函数：选中ListView条目时，回填数据到编辑框（核心需求：选中即显示）
void FillEditWithSelectedTrack(HWND hDlg) {
    if (hDlg == NULL || hTrackList == NULL) return;

    // 获取选中项
    int nSel = ListView_GetNextItem(hTrackList, -1, LVNI_SELECTED);
    if (nSel == -1) {
        // 无选中项时清空编辑框（可选，根据需求调整）
        SetDlgItemText(hDlg, IDC_TRACK_TITLE, _T(""));
        return;
    }

    // 从数组中获取数据（比从ListView读取更高效，数据更准确）
    FullTrackInfo* pTrack = &g_TrackArr[nSel];
    // 回填歌曲名到曲目编辑框
    SetDlgItemText(hDlg, IDC_TRACK_TITLE, pTrack->title);
    // 回填专辑名、作者名到对应编辑框（可选：锁定全局编辑框，仅允许在顶部修改）
    SetDlgItemText(hDlg, IDC_ALBUM_NAME, pTrack->album);
    SetDlgItemText(hDlg, IDC_ALBUM_ARTIST, pTrack->artist);
}

// 对话框过程函数（核心）
INT_PTR CALLBACK AlbumInfoDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
    {
        // 初始化控件和菜单
        hTrackList = GetDlgItem(hDlg, IDC_TRACK_LIST);
        g_TrackCount = 0; // 每次打开对话框重置曲目数
        InitTrackListView(hTrackList);
        SetWindowText(GetDlgItem(hDlg, IDC_ALBUM_ARTIST_STATIC), _T("作者名："));

        // 创建右键菜单（仅粘贴项）
        hPopupMenu = CreatePopupMenu();
        if (hPopupMenu != NULL) {
            AppendMenu(hPopupMenu, MF_STRING, IDM_PASTE, _T("粘贴曲目"));
        }
        return TRUE;
    }
    case WM_NOTIFY:
    {
        // 处理ListView的选中变更消息（NM_CLICK/NM_DBLCLK/LVN_ITEMCHANGED）
        NMHDR* pNMHDR = (NMHDR*)lParam;
        if (pNMHDR->hwndFrom == hTrackList && pNMHDR->code == LVN_ITEMCHANGED) {
            // 选中项发生变化时，回填数据到编辑框
            FillEditWithSelectedTrack(hDlg);
        }
    }
    break;

    case WM_CONTEXTMENU:
    {
        // 仅在ListView上右键时显示菜单
        if ((HWND)wParam == hTrackList && hPopupMenu != NULL) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
           // ClientToScreen(hDlg, &pt); // 精准转换为屏幕坐标
            // 显示右键菜单，确保在ListView内部/附近
            TrackPopupMenu(hPopupMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hDlg, NULL);
        }
    }
    break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_PASTE:
        {
            TCHAR szClipAll[10240] = { 0 };
            if (!GetClipboardText(szClipAll, sizeof(szClipAll) / sizeof(TCHAR))) {
                MessageBox(hDlg, _T("剪贴板无有效文本！"), _T("提示"), MB_ICONINFORMATION | MB_OK);
                break;
            }

            TCHAR szLineArr[MAX_TRACK_NUM][256] = { 0 };
            int lineNum = SplitTextByLine(szClipAll, szLineArr, MAX_TRACK_NUM, 256);
            if (lineNum == 0) {
                MessageBox(hDlg, _T("剪贴板文本无有效行！"), _T("提示"), MB_ICONINFORMATION | MB_OK);
                break;
            }

            int remainNum = MAX_TRACK_NUM - g_TrackCount;
            if (lineNum > remainNum) {
                TCHAR szTip[256] = { 0 };
                StringCchPrintf(szTip, 256, _T("最多仅支持%d首曲目，剩余%d首，将导入前%d首！"), MAX_TRACK_NUM, remainNum, remainNum);
                MessageBox(hDlg, szTip, _T("提示"), MB_ICONWARNING | MB_OK);
                lineNum = remainNum;
            }

            int successCount = 0;
            for (int i = 0; i < lineNum; i++) {
                if (AddSingleTrack(hDlg, szLineArr[i])) {
                    //在这里添加到列表框里
					SendMessage(g_hTrackList, LB_ADDSTRING, 0, (LPARAM)szLineArr[i]);
					successCount++;
                }
            }

            TCHAR szResult[256] = { 0 };
            StringCchPrintf(szResult, 256, _T("成功从剪贴板导入%d首曲目！"), successCount);
            MessageBox(hDlg, szResult, _T("完成"), MB_OK);
        }
        break;

        case IDC_ALBUM_OK:
        {
            TCHAR szAlbumName[256] = { 0 };
            TCHAR szExeDir[512] = { 0 };
            TCHAR szIniPath[1024] = { 0 };
            GetDlgItemText(hDlg, IDC_ALBUM_NAME, szAlbumName, 256);
            TCHAR szArtist[256] = { 0 };
            GetDlgItemText(hDlg, IDC_ALBUM_ARTIST, szArtist, 256);

            // 校验
            if (_tcslen(szAlbumName) == 0) {
                MessageBox(hDlg, _T("专辑名不能为空（作为INI文件名）！"), _T("错误"), MB_ICONERROR | MB_OK);
                break;
            }
            if (g_TrackCount == 0) {
                MessageBox(hDlg, _T("请至少添加1首曲目！"), _T("错误"), MB_ICONERROR | MB_OK);
                break;
            }

            // 构造INI路径
            GetExeDirectory(szExeDir, 512);
            StringCchPrintf(szIniPath, 1024, _T("%s%s.ini"), szExeDir, szAlbumName);

            // 写入INI全局信息
            WritePrivateProfileString(_T("CUE_Album"), _T("AlbumName"), szAlbumName, szIniPath);
            WritePrivateProfileString(_T("CUE_Album"), _T("Artist"), szArtist, szIniPath);
            TCHAR szTrackCount[32] = { 0 };
            StringCchPrintf(szTrackCount, 32, _T("%d"), g_TrackCount);
            WritePrivateProfileString(_T("CUE_Album"), _T("TrackCount"), szTrackCount, szIniPath);

            // 写入INI曲目信息（4列数据）
            for (int i = 0; i < g_TrackCount; i++) {
                TCHAR szSection[32] = { 0 };
                TCHAR szID[32] = { 0 };
                StringCchPrintf(szSection, 32, _T("Track_%02d"), g_TrackArr[i].index);
                StringCchPrintf(szID, 32, _T("%d"), g_TrackArr[i].index);

                WritePrivateProfileString(szSection, _T("ID"), szID, szIniPath);
                WritePrivateProfileString(szSection, _T("Title"), g_TrackArr[i].title, szIniPath);
                WritePrivateProfileString(szSection, _T("Album"), g_TrackArr[i].album, szIniPath);
                WritePrivateProfileString(szSection, _T("Artist"), g_TrackArr[i].artist, szIniPath);
            }

            // 提示成功
            TCHAR szTip[1024] = { 0 };
            StringCchPrintf(szTip, 1024, _T("CUE配置INI生成成功！\n路径：%s"), szIniPath);
            std::wstring cueMaker = std::wstring(szExeDir) + L"CueMakerState.ini";
			saveWorkStateToIni(cueMaker.c_str(), NULL, szIniPath, NULL, 0);
            MessageBox(hDlg, szTip, _T("成功"), MB_ICONINFORMATION | MB_OK);
            EndDialog(hDlg, IDOK);
        }
        break;

        case IDC_ALBUM_CANCEL:
            EndDialog(hDlg, IDCANCEL);
            break;

        case IDC_TRACK_ADD:
        {
            TCHAR szTitle[256] = { 0 };
            GetDlgItemText(hDlg, IDC_TRACK_TITLE, szTitle, 256);
            if (AddSingleTrack(hDlg, szTitle)) {
                SetDlgItemText(hDlg, IDC_TRACK_TITLE, _T(""));
                MessageBox(hDlg, _T("曲目添加成功！"), _T("完成"), MB_OK);
            }
            else {
                MessageBox(hDlg, _T("曲目名不能为空或已达最大数量！"), _T("错误"), MB_ICONERROR | MB_OK);
            }
        }
        break;

        // 新增：修改专辑名后，批量更新所有曲目专辑名
        case IDC_ALBUM_NAME:
            if (HIWORD(wParam) == EN_CHANGE && g_TrackCount > 0) {
                TCHAR szAlbum[256] = { 0 };
                GetDlgItemText(hDlg, IDC_ALBUM_NAME, szAlbum, 256);
                TCHAR szArtist[256] = { 0 };
                GetDlgItemText(hDlg, IDC_ALBUM_ARTIST, szArtist, 256);
                UpdateAllTrackAlbumAndArtist(hTrackList, szAlbum, szArtist);
            }
            break;

            // 新增：修改作者名后，批量更新所有曲目作者名
        case IDC_ALBUM_ARTIST:
            if (HIWORD(wParam) == EN_CHANGE && g_TrackCount > 0) {
                TCHAR szAlbum[256] = { 0 };
                GetDlgItemText(hDlg, IDC_ALBUM_NAME, szAlbum, 256);
                TCHAR szArtist[256] = { 0 };
                GetDlgItemText(hDlg, IDC_ALBUM_ARTIST, szArtist, 256);
                UpdateAllTrackAlbumAndArtist(hTrackList, szAlbum, szArtist);
            }
            break;

        case IDC_TRACK_DELETE:
        {
            int nSel = ListView_GetNextItem(hTrackList, -1, LVNI_SELECTED);
            if (nSel == -1) {
                MessageBox(hDlg, _T("请先选中要删除的曲目！"), _T("提示"), MB_ICONWARNING | MB_OK);
                break;
            }

            // 删除ListView项
            ListView_DeleteItem(hTrackList, nSel);
            // 数组前移，更新序号和数据
            for (int i = nSel; i < g_TrackCount - 1; i++) {
                g_TrackArr[i] = g_TrackArr[i + 1];
                g_TrackArr[i].index = i + 1;
                // 更新ListView的ID列
                TCHAR szID[32] = { 0 };
                StringCchPrintf(szID, 32, _T("%d"), i + 1);
                ListView_SetItemText(hTrackList, i, 0, szID);
            }
            g_TrackCount--;
            MessageBox(hDlg, _T("曲目删除成功！"), _T("完成"), MB_OK);
        }
        break;

        case IDC_TRACK_MOVE_UP:
        {
            int nSel = ListView_GetNextItem(hTrackList, -1, LVNI_SELECTED);
            if (nSel <= 0 || nSel == -1) break;

            // 交换ListView项
            ListView_SwapItems(hTrackList, nSel, nSel - 1);
            // 交换数组数据
            FullTrackInfo temp = g_TrackArr[nSel];
            g_TrackArr[nSel] = g_TrackArr[nSel - 1];
            g_TrackArr[nSel - 1] = temp;
            // 更新ID序号
            g_TrackArr[nSel].index = nSel + 1;
            g_TrackArr[nSel - 1].index = nSel;
            // 刷新ListView ID列
            TCHAR szID1[32] = { 0 }, szID2[32] = { 0 };
            StringCchPrintf(szID1, 32, _T("%d"), nSel);
            StringCchPrintf(szID2, 32, _T("%d"), nSel + 1);
            ListView_SetItemText(hTrackList, nSel - 1, 0, szID1);
            ListView_SetItemText(hTrackList, nSel, 0, szID2);
            // 重新选中
            ListView_SetItemState(hTrackList, nSel - 1, LVIS_SELECTED | LVIS_FOCUSED, 0xFF);
        }
        break;

        case IDC_TRACK_MOVE_DOWN:
        {
            int nSel = ListView_GetNextItem(hTrackList, -1, LVNI_SELECTED);
            if (nSel == -1 || nSel >= g_TrackCount - 1) break;

            // 交换ListView项
            ListView_SwapItems(hTrackList, nSel, nSel + 1);
            // 交换数组数据
            FullTrackInfo temp = g_TrackArr[nSel];
            g_TrackArr[nSel] = g_TrackArr[nSel + 1];
            g_TrackArr[nSel + 1] = temp;
            // 更新ID序号
            g_TrackArr[nSel].index = nSel + 1;
            g_TrackArr[nSel + 1].index = nSel + 2;
            // 刷新ListView ID列
            TCHAR szID1[32] = { 0 }, szID2[32] = { 0 };
            StringCchPrintf(szID1, 32, _T("%d"), nSel + 1);
            StringCchPrintf(szID2, 32, _T("%d"), nSel + 2);
            ListView_SetItemText(hTrackList, nSel, 0, szID1);
            ListView_SetItemText(hTrackList, nSel + 1, 0, szID2);
            // 重新选中
            ListView_SetItemState(hTrackList, nSel + 1, LVIS_SELECTED | LVIS_FOCUSED, 0xFF);
        }
        break;

        case IDC_TRACK_MODIFY:
        {
            int nSel = ListView_GetNextItem(hTrackList, -1, LVNI_SELECTED);
            if (nSel == -1) {
                MessageBox(hDlg, _T("请先选中要修改的曲目！"), _T("提示"), MB_ICONWARNING | MB_OK);
                break;
            }
            // 回填数据到编辑框（直接复用FillEditWithSelectedTrack）
            FillEditWithSelectedTrack(hDlg);
            // 删除原曲目
            ListView_DeleteItem(hTrackList, nSel);
            for (int i = nSel; i < g_TrackCount - 1; i++) {
                g_TrackArr[i] = g_TrackArr[i + 1];
                g_TrackArr[i].index = i + 1;
                TCHAR szID[32] = { 0 };
                StringCchPrintf(szID, 32, _T("%d"), i + 1);
                ListView_SetItemText(hTrackList, i, 0, szID);
            }
            g_TrackCount--;
            MessageBox(hDlg, _T("请在输入框修改信息，点击【添加】完成修改！"), _T("提示"), MB_OK);
        }
        break;
        }
        break;

    case WM_DESTROY:
        if (hPopupMenu) {
            DestroyMenu(hPopupMenu);
            hPopupMenu = NULL;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

// 调用专辑信息对话框
void ShowAlbumInfoDialog(HWND hWndParent) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    INT_PTR nRet = DialogBox(
        GetModuleHandle(NULL),
        MAKEINTRESOURCE(IDD_ALBUM_INFO),
        hWndParent,
        AlbumInfoDlgProc
    );

    if (nRet == IDOK) {
        // 后续处理（如刷新主窗口）
    }
}