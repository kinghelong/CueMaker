#include "framework.h"
#include <Mmdeviceapi.h>
#include <Audioclient.h>
#include <stdint.h>
#include <process.h>
#include <vector>

#pragma comment(lib,"Ole32.lib")
#pragma comment(lib,"Mmdevapi.lib")

#define REFTIMES_PER_SEC 10000000
#define REFTIMES_PER_MILLISEC 10000

// =======================================================
// 播放控制结构
// =======================================================
struct PlayControl {
    bool running = false;
    bool paused = false;
    bool stopRequested = false;
    LONGLONG seekPosition = -1;  // 以字节为单位
    CRITICAL_SECTION cs;
};

// =======================================================
// WAV 信息结构
// =======================================================
struct WavInfo {
    WAVEFORMATEX wf{};
    LONGLONG dataOffset = 0;
    LONGLONG dataSize = 0;

    int GetBytesPerSecond() const {
        return wf.nSamplesPerSec * wf.nBlockAlign;
    }

    int GetBytesPerFrame() const {
        return wf.nBlockAlign;
    }
};
WAVEFORMATEX g_wav;
static HANDLE g_playThread = NULL;
static PlayControl g_ctrl;
static bool g_ctrlInited = false;

extern wave_header g_wavHeader;
extern int clickX, g_currentTimeMs, g_musicLengthMs;

// =======================================================
// WAV 文件解析
// =======================================================
bool ParseWavFile(HANDLE hFile, WavInfo& info)
{
    DWORD bytesRead;
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    // 读取 RIFF header
    char chunkId[4];
    DWORD chunkSize;

    // RIFF
    if (!ReadFile(hFile, chunkId, 4, &bytesRead, NULL) || bytesRead != 4)
        return false;
    if (memcmp(chunkId, "RIFF", 4) != 0)
        return false;

    // File size
    if (!ReadFile(hFile, &chunkSize, 4, &bytesRead, NULL) || bytesRead != 4)
        return false;

    // WAVE
    if (!ReadFile(hFile, chunkId, 4, &bytesRead, NULL) || bytesRead != 4)
        return false;
    if (memcmp(chunkId, "WAVE", 4) != 0)
        return false;

    bool fmtFound = false;
    bool dataFound = false;

    // 遍历所有 chunk
    while (ReadFile(hFile, chunkId, 4, &bytesRead, NULL) && bytesRead == 4)
    {
        if (!ReadFile(hFile, &chunkSize, 4, &bytesRead, NULL) || bytesRead != 4)
            break;

        if (memcmp(chunkId, "fmt ", 4) == 0)
        {
            DWORD fmtSize = min(chunkSize, sizeof(WAVEFORMATEX));
            if (!ReadFile(hFile, &info.wf, fmtSize, &bytesRead, NULL))
                break;

            // 跳过额外的 fmt 数据
            if (chunkSize > fmtSize) {
                SetFilePointer(hFile, chunkSize - fmtSize, NULL, FILE_CURRENT);
            }
            fmtFound = true;
        }
        else if (memcmp(chunkId, "data", 4) == 0)
        {
            info.dataOffset = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
            info.dataSize = chunkSize;
            dataFound = true;
            break;  // data chunk 通常是最后一个
        }
        else
        {
            // 跳过未知 chunk
            SetFilePointer(hFile, chunkSize, NULL, FILE_CURRENT);
        }
    }

    return fmtFound && dataFound;
}

// =======================================================
// 播放线程主函数
// =======================================================
unsigned int __stdcall PlayThreadProc(void* param)
{
    wchar_t* fileName = (wchar_t*)param;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        free(fileName);
        return 1;
    }

    // COM 接口指针
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioRenderClient* pRenderClient = nullptr;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    HRESULT result = S_OK;

    do
    {
        // 打开文件
        hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            result = E_FAIL;
            break;
        }

        // 解析 WAV 文件
        WavInfo wavInfo;
        if (!ParseWavFile(hFile, wavInfo)) {
            result = E_FAIL;
            break;
        }
        g_wav = wavInfo.wf;

        // 计算音乐总时长
        if (wavInfo.GetBytesPerSecond() > 0) {
            g_musicLengthMs = (int)(wavInfo.dataSize * 1000 / wavInfo.GetBytesPerSecond());
        }

        // 创建设备枚举器
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 获取默认音频输出设备
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 激活音频客户端
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            NULL, (void**)&pAudioClient);
        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 初始化音频客户端
        REFERENCE_TIME bufferDuration = REFTIMES_PER_SEC;  // 1秒缓冲
        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            bufferDuration,
            0,
            &wavInfo.wf,
            NULL);

        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 获取缓冲区大小
        UINT32 bufferFrameCount;
        hr = pAudioClient->GetBufferSize(&bufferFrameCount);
        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 获取渲染客户端
        hr = pAudioClient->GetService(__uuidof(IAudioRenderClient),
            (void**)&pRenderClient);
        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 准备读取缓冲区
        UINT32 bytesPerFrame = wavInfo.GetBytesPerFrame();
        std::vector<BYTE> readBuffer(bufferFrameCount * bytesPerFrame);

        // 播放位置
        LONGLONG currentPosition = wavInfo.dataOffset;
        LONGLONG endPosition = wavInfo.dataOffset + wavInfo.dataSize;

        // 设置文件指针到数据开始位置
        SetFilePointer(hFile, (LONG)currentPosition, NULL, FILE_BEGIN);

        // 启动音频流
        hr = pAudioClient->Start();
        if (FAILED(hr)) {
            result = hr;
            break;
        }

        // 标记开始运行
        EnterCriticalSection(&g_ctrl.cs);
        g_ctrl.running = true;
        g_ctrl.stopRequested = false;
        LeaveCriticalSection(&g_ctrl.cs);

        // 主播放循环
        int noDataCount = 0;  // 无数据计数器，防止死循环

        while (true)
        {
            // 检查控制状态
            EnterCriticalSection(&g_ctrl.cs);
            bool shouldStop = g_ctrl.stopRequested || !g_ctrl.running;
            bool isPaused = g_ctrl.paused;
            LONGLONG seekPos = g_ctrl.seekPosition;
            g_ctrl.seekPosition = -1;  // 消费 seek 请求
            LeaveCriticalSection(&g_ctrl.cs);

            // 停止请求
            if (shouldStop) {
                break;
            }

            // 暂停处理
            if (isPaused) {
                pAudioClient->Stop();

                // 等待恢复
                while (true) {
                    Sleep(50);

                    EnterCriticalSection(&g_ctrl.cs);
                    bool stillPaused = g_ctrl.paused;
                    bool stopInPause = g_ctrl.stopRequested;
                    LeaveCriticalSection(&g_ctrl.cs);

                    if (stopInPause) {
                        shouldStop = true;
                        break;
                    }

                    if (!stillPaused) {
                        break;
                    }
                }

                if (shouldStop) {
                    break;
                }

                // 恢复播放
                pAudioClient->Start();
            }

            // Seek 处理
            if (seekPos >= 0) {
                // 停止并重置音频流
                pAudioClient->Stop();
                pAudioClient->Reset();

                // 计算新位置（对齐到帧边界）
                LONGLONG newPos = wavInfo.dataOffset + seekPos;
                newPos = (newPos / bytesPerFrame) * bytesPerFrame;  // 对齐

                // 限制范围
                if (newPos < wavInfo.dataOffset) {
                    newPos = wavInfo.dataOffset;
                }
                if (newPos > endPosition) {
                    newPos = endPosition;
                }

                currentPosition = newPos;
                SetFilePointer(hFile, (LONG)currentPosition, NULL, FILE_BEGIN);

                // 重新启动
                pAudioClient->Start();
            }

            // 检查是否播放完毕
            if (currentPosition >= endPosition) {
                break;
            }

            // 查询当前缓冲区填充情况
            UINT32 numFramesPadding;
            hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
            if (FAILED(hr)) {
                break;
            }

            // 计算可用帧数
            UINT32 numFramesAvailable = bufferFrameCount - numFramesPadding;

            if (numFramesAvailable == 0) {
                // 缓冲区已满，短暂休眠
                Sleep(5);

                noDataCount++;
                if (noDataCount > 2000) {  // 10秒无法写入，异常退出
                    break;
                }
                continue;
            }

            noDataCount = 0;  // 重置计数器

            // 计算需要读取的字节数
            UINT32 bytesToRead = numFramesAvailable * bytesPerFrame;
            LONGLONG bytesRemaining = endPosition - currentPosition;

            if (bytesToRead > bytesRemaining) {
                bytesToRead = (UINT32)bytesRemaining;
            }

            // 读取文件数据
            DWORD bytesActuallyRead = 0;
            if (!ReadFile(hFile, readBuffer.data(), bytesToRead, &bytesActuallyRead, NULL)) {
                break;
            }

            if (bytesActuallyRead == 0) {
                break;  // 文件读取结束
            }

            // 更新位置
            currentPosition += bytesActuallyRead;

            // 更新当前播放时间（毫秒）
            LONGLONG playedBytes = currentPosition - wavInfo.dataOffset;
            if (wavInfo.GetBytesPerSecond() > 0) {
                g_currentTimeMs = (int)(playedBytes * 1000 / wavInfo.GetBytesPerSecond());
            }

            // 获取音频缓冲区
            BYTE* pData = nullptr;
            UINT32 framesToWrite = bytesActuallyRead / bytesPerFrame;

            hr = pRenderClient->GetBuffer(framesToWrite, &pData);
            if (FAILED(hr)) {
                break;
            }

            // 复制数据到音频缓冲区
            memcpy(pData, readBuffer.data(), bytesActuallyRead);

            // 释放缓冲区
            hr = pRenderClient->ReleaseBuffer(framesToWrite, 0);
            if (FAILED(hr)) {
                break;
            }
        }

        // 停止播放
        pAudioClient->Stop();

        // 等待缓冲区播放完毕
        Sleep(100);

    } while (false);

    // 清理资源
    if (pRenderClient) {
        pRenderClient->Release();
    }
    if (pAudioClient) {
        pAudioClient->Release();
    }
    if (pDevice) {
        pDevice->Release();
    }
    if (pEnumerator) {
        pEnumerator->Release();
    }
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }

    free(fileName);
    CoUninitialize();

    // 清理运行状态
    EnterCriticalSection(&g_ctrl.cs);
    g_ctrl.running = false;
    g_ctrl.paused = false;
    g_ctrl.stopRequested = false;
    LeaveCriticalSection(&g_ctrl.cs);

    return SUCCEEDED(result) ? 0 : 1;
}

// =======================================================
// 对外接口实现
// =======================================================

void InitPlayer()
{
    if (!g_ctrlInited) {
        InitializeCriticalSection(&g_ctrl.cs);
        g_ctrlInited = true;
        g_ctrl.running = false;
        g_ctrl.paused = false;
        g_ctrl.stopRequested = false;
        g_ctrl.seekPosition = -1;
    }
}

void StartPlay(const wchar_t* file)
{
    if (!file) return;

    InitPlayer();

    // 停止已有的播放
    StopPlay();

    // 重置控制状态
    EnterCriticalSection(&g_ctrl.cs);
    g_ctrl.running = false;
    g_ctrl.paused = false;
    g_ctrl.stopRequested = false;
    g_ctrl.seekPosition = -1;
    LeaveCriticalSection(&g_ctrl.cs);

    // 复制文件名
    wchar_t* fileNameCopy = _wcsdup(file);
    if (!fileNameCopy) return;

    // 创建播放线程
    g_playThread = (HANDLE)_beginthreadex(
        NULL,
        0,
        PlayThreadProc,
        fileNameCopy,
        0,
        NULL
    );
}

void PausePlay()
{
    if (!g_playThread) return;

    EnterCriticalSection(&g_ctrl.cs);
    g_ctrl.paused = !g_ctrl.paused;
    LeaveCriticalSection(&g_ctrl.cs);
}

void SeekPlay(double seconds, int sampleRate, int channels, int bitsPerSample)
{
    if (!g_playThread) return;

    // 计算字节偏移
    LONGLONG bytesPerSecond = (LONGLONG)sampleRate * channels * (bitsPerSample / 8);
    LONGLONG seekBytes = (LONGLONG)(seconds * bytesPerSecond);

    EnterCriticalSection(&g_ctrl.cs);
    g_ctrl.seekPosition = seekBytes;
    LeaveCriticalSection(&g_ctrl.cs);
}

void StopPlay()
{
    if (!g_playThread) return;

    // 请求停止
    EnterCriticalSection(&g_ctrl.cs);
    g_ctrl.running = false;
    g_ctrl.stopRequested = true;
    g_ctrl.paused = false;  // 如果在暂停状态，取消暂停
    LeaveCriticalSection(&g_ctrl.cs);

    // 等待线程结束（最多等待3秒）
    DWORD waitResult = WaitForSingleObject(g_playThread, 3000);

    if (waitResult == WAIT_TIMEOUT) {
        // 超时则强制终止（不推荐，但作为最后手段）
        TerminateThread(g_playThread, 1);
    }

    CloseHandle(g_playThread);
    g_playThread = NULL;

    // 重置当前时间
    g_currentTimeMs = 0;
}

// =======================================================
// 可选：获取播放状态
// =======================================================
bool IsPlaying()
{
    bool result = false;
    EnterCriticalSection(&g_ctrl.cs);
    result = g_ctrl.running && !g_ctrl.paused;
    LeaveCriticalSection(&g_ctrl.cs);
    return result;
}

bool IsPaused()
{
    bool result = false;
    EnterCriticalSection(&g_ctrl.cs);
    result = g_ctrl.paused;
    LeaveCriticalSection(&g_ctrl.cs);
    return result;
}