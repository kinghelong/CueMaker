// includes
#include "framework.h"
#include "stdio.h"
#include "All.h"
#include "MACDll.h"
#include "MACLib.h"

/***************************************************************************************
  常量定义（固定配置，便于修改，无复杂数据类型）
***************************************************************************************/
#define APE_DECODE_BUFFER_BLOCKS  10240  // 单次解码块数
#define APE_DEFAULT_THREAD_COUNT  10     // 默认解码线程数

std::wstring extractFileName;
/***************************************************************************************
  APE_INFO structure (仅存放音频核心信息，必要定义，无嵌套，便于调试)
***************************************************************************************/
struct APE_INFO
{
    APE::int64 nSampleRate;    // 采样率
    APE::int64 nChannels;      // 声道数
    APE::int64 nBitsPerSample; // 位深
    APE::int64 nBlockAlign;    // 块对齐
    APE::int64 nTotalBlocks;   // 总块数
    APE::int64 nTotalDataBytes;// 音频数据总字节数
};

/***************************************************************************************
  GetFunctions - 从APE DLL中获取所有需要的函数指针（无修改，保留原有逻辑）
***************************************************************************************/
int GetFunctions(HMODULE hMACDll, MAC_DLL* pMACDll)
{
    // clear
    memset(pMACDll, 0, sizeof(MAC_DLL));

    // load the functions
    if (hMACDll != NULL)
    {
        pMACDll->Create = (proc_APEDecompress_CreateW)GetProcAddress(hMACDll, "c_APEDecompress_CreateW");
        pMACDll->Destroy = (proc_APEDecompress_Destroy)GetProcAddress(hMACDll, "c_APEDecompress_Destroy");
        pMACDll->GetData = (proc_APEDecompress_GetData)GetProcAddress(hMACDll, "c_APEDecompress_GetData");
        pMACDll->Seek = (proc_APEDecompress_Seek)GetProcAddress(hMACDll, "c_APEDecompress_Seek");
        pMACDll->GetInfo = (proc_APEDecompress_GetInfo)GetProcAddress(hMACDll, "c_APEDecompress_GetInfo");
        pMACDll->SetNumberOfThreads = (void(__stdcall*)(APE_DECOMPRESS_HANDLE, int))GetProcAddress(hMACDll, "c_APEDecompress_SetNumberOfThreads");
    }

    // error check
    if ((pMACDll->Create == NULL) ||
        (pMACDll->Destroy == NULL) ||
        (pMACDll->GetData == NULL) ||
        (pMACDll->Seek == NULL) ||
        (pMACDll->GetInfo == NULL) ||
        (pMACDll->SetNumberOfThreads == NULL))
    {
        return -1;
    }

    return 0;
}

/***************************************************************************************
  VersionCheckInterface - 校验APE DLL接口版本兼容性（无修改，保留原有逻辑）
***************************************************************************************/
int VersionCheckInterface(HMODULE hMACDll)
{
    int nRetVal = -1;
    proc_GetInterfaceCompatibility GetInterfaceCompatibility = (proc_GetInterfaceCompatibility)GetProcAddress(hMACDll, "GetInterfaceCompatibility");
    if (GetInterfaceCompatibility)
    {
        nRetVal = GetInterfaceCompatibility(APE_FILE_VERSION_NUMBER, TRUE, NULL);
    }

    return nRetVal;
}

/***************************************************************************************
  getDecompressMoudle - 修复：通过输出参数返回DLL句柄和函数指针，解决内存泄漏
  说明：不再值传递MACDll，改为输出参数，同时返回hMACDll，便于后续释放资源
***************************************************************************************/
APE_DECOMPRESS_HANDLE getDecompressMoudle(HWND hWnd, wchar_t* pFileName, HMODULE* phMACDll, MAC_DLL* pMACDll)
{
    // 严格参数合法性检查（新增phMACDll、pMACDll校验）
    if (pFileName == NULL || lstrlen(pFileName) == 0 || phMACDll == NULL || pMACDll == NULL)
    {
        MessageBox(hWnd, L"Error: Invalid parameter (empty or NULL)\r\n", L"error", MB_OK);
        return NULL;
    }

    // 初始化输出参数，避免垃圾数据
    *phMACDll = NULL;
    memset(pMACDll, 0, sizeof(MAC_DLL));

    // 加载DLL
#if _WIN64
    * phMACDll = LoadLibrary(_T("C:\\Windows\\System32\\MACDll64.dll"));
#else
    * phMACDll = LoadLibrary(_T("C:\\Windows\\SysWOW64\\MACDll.dll"));
#endif
    if (*phMACDll == NULL)
    {
        MessageBox(hWnd, L"Error: Failed to load MACDll.dll\r\n", L"error", MB_OK);
        return NULL;
    }

    // 版本校验
    if (VersionCheckInterface(*phMACDll) != 0)
    {
        MessageBox(hWnd, L"Error: Interface version incompatible\r\n", L"error", MB_OK);
        FreeLibrary(*phMACDll);
        *phMACDll = NULL;
        return NULL;
    }

    // 获取函数指针（传入输出参数pMACDll，回传函数指针）
    if (GetFunctions(*phMACDll, pMACDll) != 0)
    {
        MessageBox(hWnd, L"Error: Failed to get DLL functions\r\n", L"error", MB_OK);
        FreeLibrary(*phMACDll);
        *phMACDll = NULL;
        return NULL;
    }

    // 创建解压缩句柄
    int nRetVal = 0;
    APE_DECOMPRESS_HANDLE hAPEDecompress = pMACDll->Create(pFileName, &nRetVal);
    if (hAPEDecompress == NULL)
    {
        TCHAR msg[255] = { 0 };
        swprintf_s(msg, L"Error opening APE file (error code: %d)\r\n", nRetVal);
        MessageBox(hWnd, msg, L"Error", MB_OK);
        FreeLibrary(*phMACDll);
        *phMACDll = NULL;
        memset(pMACDll, 0, sizeof(MAC_DLL));
        return NULL;
    }

    // 配置多线程（创建句柄后立即配置，时机更合理）
    pMACDll->SetNumberOfThreads(hAPEDecompress, APE_DEFAULT_THREAD_COUNT);

    // 返回有效解压句柄，DLL句柄和函数指针通过输出参数回传
    return hAPEDecompress;
}

/***************************************************************************************
  getWavHeader - 规整：移除冗余局部变量，增加参数校验，提升健壮性
***************************************************************************************/
APE_INFO getWavHeader(APE_DECOMPRESS_HANDLE hAPEDecompress, MAC_DLL MACDll)
{
    // 初始化返回值，避免垃圾数据
    APE_INFO APE_Header = { 0 };

    // 参数合法性校验，避免空指针崩溃
    if (hAPEDecompress == NULL || MACDll.GetInfo == NULL)
    {
        return APE_Header;
    }

    // 提取音频信息（移除冗余局部变量，直接赋值给返回结构体）
    APE_Header.nSampleRate = MACDll.GetInfo(hAPEDecompress, APE::IAPEDecompress::APE_INFO_SAMPLE_RATE, 0, 0);
    APE_Header.nChannels = MACDll.GetInfo(hAPEDecompress, APE::IAPEDecompress::APE_INFO_CHANNELS, 0, 0);
    APE_Header.nBitsPerSample = MACDll.GetInfo(hAPEDecompress, APE::IAPEDecompress::APE_INFO_BITS_PER_SAMPLE, 0, 0);
    APE_Header.nBlockAlign = MACDll.GetInfo(hAPEDecompress, APE::IAPEDecompress::APE_INFO_BLOCK_ALIGN, 0, 0);
    APE_Header.nTotalBlocks = MACDll.GetInfo(hAPEDecompress, APE::IAPEDecompress::APE_DECOMPRESS_TOTAL_BLOCKS, 0, 0);
    APE_Header.nTotalDataBytes = MACDll.GetInfo(hAPEDecompress, APE::IAPEDecompress::APE_INFO_WAV_DATA_BYTES, 0, 0);

    // 补充：手动计算总数据字节数（防止GetInfo获取失败）
    if (APE_Header.nTotalDataBytes <= 0 && APE_Header.nTotalBlocks > 0 && APE_Header.nBlockAlign > 0)
    {
        APE_Header.nTotalDataBytes = APE_Header.nTotalBlocks * APE_Header.nBlockAlign;
    }

    return APE_Header;
}

/***************************************************************************************
  GetRealTimePcm - 修复：返回值逻辑正确，块数计算基于块对齐，移除硬编码
***************************************************************************************/
BOOL GetRealTimePcm(unsigned char* pcm, int pcmSize, APE_DECOMPRESS_HANDLE hAPEDecompress, MAC_DLL MACDll, APE::int64 timeBlock, APE::int64 blockAlign)
{
    // 1. 严格参数合法性校验
    if (pcm == NULL || pcmSize <= 0 || hAPEDecompress == NULL ||
        MACDll.GetData == NULL || MACDll.Seek == NULL || blockAlign <= 0)
    {
        return FALSE;
    }

    // 2. 定位到指定块
    MACDll.Seek(hAPEDecompress, timeBlock);

    // 3. 正确计算块数（基于块对齐，不再硬编码/4）
    APE::int64 nBlocksRetrieved = pcmSize / blockAlign;
    if (nBlocksRetrieved <= 0)
    {
        return FALSE;
    }

    // 4. 解码获取PCM数据
    int nRetVal = MACDll.GetData(hAPEDecompress, pcm, nBlocksRetrieved, &nBlocksRetrieved);

    // 5. 正确判断返回结果（解码成功且获取到有效块数）
    if (nRetVal == 0 && nBlocksRetrieved > 0)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/***************************************************************************************
  APE_Decompress_WAV - 修复：移除冗余逻辑，增加合法性校验，优化资源释放
***************************************************************************************/
int APE_Decompress_WAV(MAC_DLL MACDll, HMODULE hMACDll, HWND hWnd, WCHAR* pFilename, APE_DECOMPRESS_HANDLE hAPEDecompress)
{
    // 1. 前置参数合法性校验
    if (hAPEDecompress == NULL || hMACDll == NULL || MACDll.Destroy == NULL ||
        pFilename == NULL || lstrlen(pFilename) == 0)
    {
        MessageBox(hWnd, L"Error: Invalid input parameter\r\n", L"error", MB_OK);
        return -1;
    }

    // 2. 初始化变量（独立变量，便于调试）
    LARGE_INTEGER liStart, liEnd, liFrequency;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    unsigned char* pBuffer = NULL;
    APE_INFO apeInfo = { 0 };
    wave_header wavHeader = { 0 };
    APE::int64 nBlocksRetrieved = 1;
    int nRetVal = 0;
    int nWavHeaderRet = 0;
    double dTotalTimeSec = 0.000;
    TCHAR msg[255] = { 0 };
    DWORD dwBytesWritten = 0;
    TCHAR szResultMsg[512] = { 0 };

    // 3. 高精度计时初始化
    QueryPerformanceFrequency(&liFrequency);
    QueryPerformanceCounter(&liStart);

    // 4. 提取APE音频信息
    apeInfo = getWavHeader(hAPEDecompress, MACDll);
    if (apeInfo.nSampleRate <= 0 || apeInfo.nChannels <= 0 || apeInfo.nBlockAlign <= 0 || apeInfo.nTotalDataBytes <= 0)
    {
        MessageBox(hWnd, L"Error: Failed to extract APE audio information\r\n", L"error", MB_OK);
        goto Cleanup;
    }

    // 5. 创建WAV输出文件
    hFile = CreateFile(pFilename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        MessageBox(hWnd, L"Error: Failed to create WAV output file\r\n", L"error", MB_OK);
        goto Cleanup;
    }

    // 6. 写入WAV文件头
    nWavHeaderRet = write_wav_header(hFile, &wavHeader, (int)apeInfo.nSampleRate, (int)apeInfo.nChannels, (int)apeInfo.nTotalDataBytes, (int)apeInfo.nBitsPerSample);
    if (nWavHeaderRet != 0)
    {

        swprintf_s(msg, L"Error: Failed to write WAV header (error code: %d)\r\n", nWavHeaderRet);
        MessageBox(hWnd, msg, L"Error", MB_OK);
        goto Cleanup;
    }

    // 7. 分配解码缓冲区
    pBuffer = new unsigned char[size_t(APE_DECODE_BUFFER_BLOCKS * apeInfo.nBlockAlign)];
    if (pBuffer == NULL)
    {
        MessageBox(hWnd, L"Error: Failed to allocate buffer memory\r\n", L"error", MB_OK);
        goto Cleanup;
    }

    // 8. 循环解码并写入WAV文件数据段

    while (nBlocksRetrieved > 0)
    {
        // 解码数据
        nRetVal = MACDll.GetData(hAPEDecompress, pBuffer, APE_DECODE_BUFFER_BLOCKS, &nBlocksRetrieved);
        if (nRetVal != 0)
        {
            MessageBox(hWnd, L"Decompression error (continuing with write, but file is probably corrupt)\r\n", L"error", MB_OK);
        }

        // 写入有效音频数据
        if (nBlocksRetrieved > 0)
        {

            WriteFile(hFile, pBuffer, (DWORD)(nBlocksRetrieved * apeInfo.nBlockAlign), &dwBytesWritten, NULL);
        }
    }

    // 9. 记录结束时间并格式化结果
    QueryPerformanceCounter(&liEnd);
    dTotalTimeSec = (double)(liEnd.QuadPart - liStart.QuadPart) / liFrequency.QuadPart;


    swprintf_s(szResultMsg, sizeof(szResultMsg) / sizeof(TCHAR),
        L"操作完成！\r\n\r\n"
        L"使用线程数：%d\r\n"
        L"输出WAV文件：%s\r\n"
        L"音频信息：%lld Hz / %lld 声道 / %lld 位深\r\n"
        L"总用时：%.3f 秒（%.1f 毫秒）",
        APE_DEFAULT_THREAD_COUNT,
        pFilename,
        apeInfo.nSampleRate, apeInfo.nChannels, apeInfo.nBitsPerSample,
        dTotalTimeSec,
        dTotalTimeSec * 1000);

    MessageBox(hWnd, szResultMsg, L"操作成功", MB_OK | MB_ICONINFORMATION);
    extractFileName = pFilename;
    // 10. 统一资源释放（避免遗漏）
Cleanup:
    if (pBuffer != NULL)
    {
        delete[] pBuffer;
    }
    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
    }
    if (hAPEDecompress != NULL)
    {
        MACDll.Destroy(hAPEDecompress);
    }
    if (hMACDll != NULL)
    {
        FreeLibrary(hMACDll);
    }

    return (nRetVal == 0) ? 0 : -10;
}

int GetWaveOrPcm(HWND hWnd, std::wstring fileName, int mode)
{
    // 1. 前置参数校验（新增：避免空文件名、无效窗口句柄）
    if (hWnd == NULL || fileName.empty())
    {
        MessageBox(hWnd, L"错误：无效的输入参数（窗口句柄为空或文件名为空）", L"参数错误", MB_OK | MB_ICONERROR);
        return -1; // 非法参数，直接返回
    }

    HMODULE hMACDll = NULL;
    MAC_DLL MACDll = { 0 };
    APE_DECOMPRESS_HANDLE hAPEDecompress = NULL;

    std::wstring outputFileName;
    outputFileName = ReplaceExt(fileName, L".wav");

    // 2. 获取解压句柄和 DLL 函数（逻辑不变，保留原有调用）
     hAPEDecompress = getDecompressMoudle(
        hWnd,
        const_cast<wchar_t*>(fileName.c_str()), // 强制去掉 const
        &hMACDll,
        &MACDll
    );

    // 3. 校验解压句柄是否获取成功（逻辑不变）
    if (hAPEDecompress == NULL)
    {
        MessageBox(hWnd, L"错误：获取APE解压句柄失败", L"初始化失败", MB_OK | MB_ICONERROR);
        return -1; // 获取失败，直接返回
    }

    int ret = -1; // 初始化返回值为错误码，避免未赋值风险

    // 4. 分支处理（修复：参数转换 + 完善默认分支）
    if (mode == 1) // 解压成 WAV
    {
        ret = APE_Decompress_WAV(MACDll, hMACDll, hWnd, const_cast<WCHAR*>(outputFileName.c_str()), hAPEDecompress);   
    }
    else if (mode == 2) // 解压成 PCM（待实现）
    {
        APE_INFO apeInfo = { 0 };
        apeInfo = getWavHeader(hAPEDecompress, MACDll);
        if (apeInfo.nSampleRate <= 0 || apeInfo.nChannels <= 0 || apeInfo.nBlockAlign <= 0 || apeInfo.nTotalDataBytes <= 0)
        {
            MessageBox(hWnd, L"Error: Failed to extract APE audio information\r\n", L"error", MB_OK);
        }

    }
    else // 新增：默认分支（无效mode）
    {
        MessageBox(hWnd, L"错误：无效的操作模式（仅支持1=解压WAV，2=解压PCM）", L"模式错误", MB_OK | MB_ICONERROR);
        ret = -2; // 无效模式，返回独立错误码，便于排查

        // 注意：这里需要手动释放资源（因为没有进入APE_Decompress_WAV，内部未释放）
        if (hAPEDecompress != NULL)
        {
            MACDll.Destroy(hAPEDecompress);
        }
        if (hMACDll != NULL)
        {
            FreeLibrary(hMACDll);
        }
    }

    // 说明：
    // 1. 当mode=1时，APE_Decompress_WAV内部已经释放了 hAPEDecompress 和 hMACDll，外部无需再释放
    // 2. 当mode=2或其他值时，需要在对应分支内手动释放资源，避免内存泄漏

    return ret;
}