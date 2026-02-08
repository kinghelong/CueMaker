#include"framework.h"
#include"Macro.h"
#include"ToobarFun.h"
#include<commdlg.h>
#include<Shlwapi.h> 
#pragma comment(lib,"Shlwapi.lib")

WorkContext g_Work;
std::wstring g_cueFileContent;
std::wstring g_apeFilePath,g_cueFilePath;
bool OpenAudioFileDialog(HWND hWnd, std::wstring& audioPath)
{
    wchar_t szFile[MAX_PATH] = { 0 };

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"音频文件 (*.flac;*.wav;*.ape)\0*.flac;*.wav;*.ape\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择音频文件";

    if (GetOpenFileName(&ofn))
    {
        audioPath = szFile;
        return true;
    }
    return false;
}

// 声明 OpenExistingCue 函数，假设它的实现会在其他地方提供
bool  OpenExistingCue(const std::wstring& cuePath)
{
    HANDLE hFile = 0;
    hFile = CreateFile(cuePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD dwBytesRead = 0, fileSize = 0;
    fileSize = GetFileSize(hFile, NULL);

    // 修复：确保 g_cueFileContent 有足够空间读取文件内容
    g_cueFileContent.resize(fileSize / sizeof(wchar_t));
	g_cueFileContent.clear();
    BOOL bSuccess = ReadFile(hFile, &g_cueFileContent[0], fileSize, &dwBytesRead, NULL);
    CloseHandle(hFile);

    if (!bSuccess)
        return false;

    if (dwBytesRead == fileSize)
        return true;
    else
        return false;
}
HANDLE WriteNewCue(const std::wstring& cuePath)
{
    HANDLE hFile = CreateFile(cuePath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) ;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        // 这里可以添加写入新的 CUE 文件的代码
        CloseHandle(hFile);
    }
    //cue文件用不用在开头写上unicode BOM？
    return hFile;
}
std::wstring ReplaceExt(const std::wstring&srcPath,std::wstring ext)
{
    // 简单实现：替换文件扩展名
    size_t pos = srcPath.find_last_of(L'.');
    if (pos != std::wstring::npos)
    {		
        return srcPath.substr(0, pos) + ext;
    }
	return NULL;   
}

bool  OnNewCue(HWND hWnd)
{
    // 1. 选择音频文件
    std::wstring audioPath;
    if (!OpenAudioFileDialog(hWnd, audioPath))
        return false;
	g_apeFilePath = audioPath;

    // 2. 生成 cue 路径
    std::wstring cuePath = ReplaceExt(audioPath, L".cue");

    // 3. 检查 cue 是否存在
    bool cueExists = PathFileExists(cuePath.c_str());
    if (cueExists)
    {
        int ret = MessageBox(hWnd, L"CUE 文件已存在，是否覆盖？", L"提示", MB_YESNOCANCEL | MB_ICONQUESTION);

        if (ret == IDCANCEL)
            return false;

        if (ret == IDNO)
        {
            if(OpenExistingCue(cuePath))
                return true;
        }
    }

    // 4. 初始化工作上下文
    g_Work.audioPath = audioPath;
    g_Work.cuePath = cuePath;
    g_Work.hasCueFile = cueExists;

   // ResetAlbumInfo();
    //ResetTrackList();
    //ResetProgress();
	return true;
}