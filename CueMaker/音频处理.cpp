#define NOMINMAX

#include"framework.h"
#include"Macro.h"
#include <thread>
#include <vector>
#include <future>

wave_header g_wavHeader;
std::vector<int16_t> g_leftPcmData;
std::vector<int16_t> g_rightPcmData;
bool g_isWavLoaded = false;
BYTE channels = 0;
int musicLengthInMs = 0, musicLengthInS = 0;

extern double g_zoomFactor;
extern double g_scrollOffset;
extern int clickX, g_currentTimeMs, g_musicLengthMs;
extern bool g_isPlaying;
extern std::vector<MuteRange> timeList;

// ===== 新增：存储点击位置对应的实际音频时间 =====
extern double g_clickTimeRatio ;  // 点击位置对应的时间比例 (0.0 ~ 1.0)

bool DrawTimeLine(HDC hdc, RECT rect, double musicLengthInS);

bool LoadWavFile(const std::wstring& wavPath)
{
    g_leftPcmData.clear();
    g_rightPcmData.clear();
    g_isWavLoaded = false;
    g_clickTimeRatio = -1.0;  // 重置点击位置

    if (wavPath.empty())
    {
        MessageBox(NULL, L"没有文件", L"错误", MB_OK);
        return false;
    }

    HANDLE hFile = CreateFile(wavPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        MessageBox(NULL, L"无法打开文件", L"错误", MB_OK);
        return false;
    }

    // 1. 读取 WAV 头
    if (read_wav_header(hFile, &g_wavHeader) != 0)
    {
        MessageBox(NULL, L"无效的WAV文件", L"错误", MB_OK);
        CloseHandle(hFile);
        return false;
    }

    // 读取原始字节
    DWORD bytesToRead = g_wavHeader.data_size;
    std::vector<uint8_t> rawBuffer(bytesToRead);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, rawBuffer.data(), bytesToRead, &bytesRead, NULL) ||
        bytesRead != bytesToRead)
    {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    int numChannels = g_wavHeader.channels;
    int bitsPerSample = g_wavHeader.bits_per_sample;
    int bytesPerSample = bitsPerSample / 8;
    int samplesPerChannel = (bytesRead / bytesPerSample) / numChannels;
    g_musicLengthMs = (g_wavHeader.data_size * 1000) / g_wavHeader.byterate;

    // 直接 Resize
    g_leftPcmData.assign(samplesPerChannel, 0);
    g_rightPcmData.assign(samplesPerChannel, 0);

    // 并行解析
    int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    int chunkSize = samplesPerChannel / numThreads;
    std::vector<std::future<void>> futures;

    for (int t = 0; t < numThreads; ++t)
    {
        int startSample = t * chunkSize;
        int endSample = (t == numThreads - 1) ? samplesPerChannel : (t + 1) * chunkSize;

        futures.push_back(std::async(std::launch::async, [=, &rawBuffer]() {
            if (bitsPerSample == 8) {
                for (int i = startSample; i < endSample; ++i) {
                    int rawIdx = i * numChannels;
                    g_leftPcmData[i] = (static_cast<int16_t>(rawBuffer[rawIdx]) - 128) << 8;
                    if (numChannels == 2)
                        g_rightPcmData[i] = (static_cast<int16_t>(rawBuffer[rawIdx + 1]) - 128) << 8;
                    else
                        g_rightPcmData[i] = g_leftPcmData[i];
                }
            }
            else if (bitsPerSample == 16) {
                const int16_t* pcm16 = reinterpret_cast<const int16_t*>(rawBuffer.data());
                for (int i = startSample; i < endSample; ++i) {
                    int pcmIdx = i * numChannels;
                    g_leftPcmData[i] = pcm16[pcmIdx];
                    if (numChannels == 2)
                        g_rightPcmData[i] = pcm16[pcmIdx + 1];
                    else
                        g_rightPcmData[i] = g_leftPcmData[i];
                }
            }
            }));
    }

    // 等待所有线程完成
    for (auto& f : futures) f.wait();

    // 更新时长信息
    musicLengthInS = static_cast<int>(g_wavHeader.data_size / g_wavHeader.byterate);
    musicLengthInMs = musicLengthInS * 1000;
    g_isWavLoaded = true;

    return true;
}

void DrawSingleWaveform(HDC hdc, RECT rect, const std::vector<int16_t>& data, COLORREF color)
{
    if (data.empty()) return;

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    int centerY = rect.top + h / 2;
    double zoomedTotalWidth = w * g_zoomFactor;

    // 计算当前可见的数据范围
    size_t startIdx = (size_t)((g_scrollOffset / zoomedTotalWidth) * data.size());
    size_t endIdx = (size_t)(((g_scrollOffset + w) / zoomedTotalWidth) * data.size());

    if (startIdx >= data.size()) {
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        return;
    }
    if (endIdx > data.size()) endIdx = data.size();

    double samplesPerPixel = (double)(endIdx - startIdx) / w;

    for (int x = 0; x < w; x++) {
        size_t dataIdx = startIdx + (size_t)(x * samplesPerPixel);
        if (dataIdx >= data.size()) break;

        int lineHeight = (int)((data[dataIdx] / 32768.0) * (h / 2.0));

        if (x == 0)
            MoveToEx(hdc, rect.left + x, centerY - lineHeight, NULL);
        else
            LineTo(hdc, rect.left + x, centerY - lineHeight);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void CalculateFinalLayout(RECT clientRect, int numChannels, RECT& waveArea,
    RECT& timeArea, ChannelLayout& layout)
{
    // 底部固定留出 20 像素给时间轴
    timeArea = clientRect;
    timeArea.top = clientRect.bottom - 20;

    // 剩余部分给波形
    waveArea = clientRect;
    waveArea.bottom = timeArea.top;

    layout.isStereo = (numChannels >= 2);
    if (!layout.isStereo) {
        layout.rectLeft = waveArea;
        SetRectEmpty(&layout.rectRight);
    }
    else {
        int waveHeight = waveArea.bottom - waveArea.top;
        int gapHeight = 10;
        int channelHeight = (waveHeight - gapHeight) / 2;

        layout.rectLeft = waveArea;
        layout.rectLeft.bottom = waveArea.top + channelHeight;

        layout.rectRight = waveArea;
        layout.rectRight.top = layout.rectLeft.bottom + gapHeight;
    }
}

void DrawDualChannelWaveform(HDC hdc, HWND hWnd)
{
    if (!g_isWavLoaded || g_leftPcmData.empty()) return;

    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    int width = rcClient.right - rcClient.left;
    int height = rcClient.bottom - rcClient.top;
    if (width <= 0 || height <= 0) return;

    // --- 1. 双缓冲初始化 ---
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 背景涂黑
    FillRect(memDC, &rcClient, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // 计算布局 (假设你已有的逻辑)
    RECT waveArea, timeArea;
    ChannelLayout layout;
    CalculateFinalLayout(rcClient, g_wavHeader.channels, waveArea, timeArea, layout);

    // 绘制时间轴 (略过，假设正常)
    DrawTimeLine(memDC, timeArea, g_musicLengthMs / 1000.0);

    // --- 2. 核心坐标映射参数 ---
    // 总宽度 = 基础宽度 * 缩放因子
    double totalWidth = (double)width * g_zoomFactor;
    // 总采样点数
    size_t totalSamples = g_leftPcmData.size();

    // Lambda: 将采样点索引转换为当前屏幕的 X 坐标
    auto SampleToX = [&](INT64 sampleIdx) -> int {
        double ratio = (double)sampleIdx / totalSamples;
        return (int)(ratio * totalWidth - g_scrollOffset);
        };

    // --- 3. 绘制静音高亮区 (AlphaBlend) ---
    if (!timeList.empty()) {
        HDC tempDC = CreateCompatibleDC(memDC);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 160, 0 }; // 160 为透明度
        HBRUSH hMuteBrush = CreateSolidBrush(RGB(128, 0, 255));

        for (const auto& zone : timeList) {
            int startX = SampleToX(zone.start);
            int endX = SampleToX(zone.end);

            // 裁剪：只画在可见区域内的
            if (endX < 0 || startX > width) continue;
            int drawX = std::max(0, startX);
            int drawW = std::min(width, endX) - drawX;
            if (drawW <= 0) continue;

            // 对应波形区的高度
            int drawH = waveArea.bottom - waveArea.top;

            // 创建用于混合的临时位图
            HBITMAP tempBmp = CreateCompatibleBitmap(memDC, drawW, drawH);
            HBITMAP oldTempBmp = (HBITMAP)SelectObject(tempDC, tempBmp);

            RECT fillR = { 0, 0, drawW, drawH };
            FillRect(tempDC, &fillR, hMuteBrush);

            AlphaBlend(memDC, drawX, waveArea.top, drawW, drawH,
                tempDC, 0, 0, drawW, drawH, bf);

            SelectObject(tempDC, oldTempBmp);
            DeleteObject(tempBmp);
        }
        DeleteObject(hMuteBrush);
        DeleteDC(tempDC);
    }

    // --- 4. 绘制波形 (带步长优化) ---
    auto DrawWave = [&](RECT rect, const std::vector<short>& data, COLORREF color) {
        if (data.empty()) return;
        HPEN hPen = CreatePen(PS_SOLID, 1, color);
        HPEN oldPen = (HPEN)SelectObject(memDC, hPen);

        int midY = rect.top + (rect.bottom - rect.top) / 2;
        int maxHeight = (rect.bottom - rect.top) / 2;

        // 根据缩放决定步长：1个像素可能对应成千上万个点
        // 步长 = (总采样点 / 总宽度) 保证每个像素只画一次，防止卡死
        double samplesPerPixel = totalSamples / totalWidth;
        int step = std::max(1, (int)samplesPerPixel);

        MoveToEx(memDC, SampleToX(0), midY, NULL);

        // 只遍历可见范围内的采样点
        INT64 startIdx = (INT64)(g_scrollOffset / totalWidth * totalSamples);
        INT64 endIdx = (INT64)((g_scrollOffset + width) / totalWidth * totalSamples);
        startIdx = std::max((INT64)0, startIdx);
        endIdx = std::min((INT64)totalSamples, endIdx);

        for (INT64 i = startIdx; i < endIdx; i += step) {
            int x = SampleToX(i);
            // 将 16-bit PCM (-32768 ~ 32767) 映射到 Y 坐标
            int yOffset = (int)((double)data[i] / 32768.0 * maxHeight);
            LineTo(memDC, x, midY - yOffset);
        }

        SelectObject(memDC, oldPen);
        DeleteObject(hPen);
        };

    DrawWave(layout.rectLeft, g_leftPcmData, RGB(0, 255, 0));
    if (layout.isStereo) {
        DrawWave(layout.rectRight, g_rightPcmData, RGB(255, 255, 0));

        // 装饰线
        HPEN darkPen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
        HPEN oldP = (HPEN)SelectObject(memDC, darkPen);
        int midY = layout.rectLeft.bottom + (layout.rectRight.top - layout.rectLeft.bottom) / 2;
        MoveToEx(memDC, 0, midY, NULL); LineTo(memDC, width, midY);
        SelectObject(memDC, oldP); DeleteObject(darkPen);
    }

    // --- 5. 绘制播放线 ---
    double playRatio = g_isPlaying ? ((double)g_currentTimeMs / g_musicLengthMs) : g_clickTimeRatio;
    int playX = (int)(playRatio * totalWidth - g_scrollOffset);

    if (playX >= 0 && playX <= width) {
        HPEN pPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
        HPEN oPen = (HPEN)SelectObject(memDC, pPen);
        MoveToEx(memDC, playX, 0, NULL); LineTo(memDC, playX, height);
        SelectObject(memDC, oPen); DeleteObject(pPen);
    }

    // --- 6. 拷贝输出与清理 ---
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}
bool DrawTimeLine(HDC hdc, RECT rect, double musicLengthInS)
{
    int width = rect.right - rect.left;
    double zoomedTotalWidth = width * g_zoomFactor;

    // 计算当前屏幕左侧和右侧对应音频的秒数
    double startTime = (g_scrollOffset / zoomedTotalWidth) * musicLengthInS;
    double endTime = ((g_scrollOffset + width) / zoomedTotalWidth) * musicLengthInS;
    double visibleDuration = endTime - startTime;

    // 根据可见时长动态调整刻度密度
    int finalInterval = 1;
    if (visibleDuration > 600) finalInterval = 60;
    else if (visibleDuration > 120) finalInterval = 30;
    else if (visibleDuration > 30) finalInterval = 5;
    else if (visibleDuration > 5) finalInterval = 1;

    // 设置文字颜色
    SetTextColor(hdc, RGB(200, 200, 200));
    SetBkMode(hdc, TRANSPARENT);

    // 绘制刻度
    HPEN timePen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HPEN oldPen = (HPEN)SelectObject(hdc, timePen);

    for (int s = (int)startTime; s <= (int)endTime; s++)
    {
        if (s % finalInterval == 0)
        {
            // 计算 X 坐标
            int x = (int)((s / musicLengthInS) * zoomedTotalWidth - g_scrollOffset);

            // 绘制刻度线
            MoveToEx(hdc, x, rect.top, NULL);
            LineTo(hdc, x, rect.top + 5);

            // 绘制时间文字
            wchar_t timeStr[16];
            swprintf_s(timeStr, L"%02d:%02d", s / 60, s % 60);
            RECT textRect = { x - 25, rect.top + 5, x + 25, rect.bottom };
            DrawText(hdc, timeStr, -1, &textRect, DT_CENTER | DT_SINGLELINE | DT_TOP);
        }
    }

    SelectObject(hdc, oldPen);
    DeleteObject(timePen);

    return true;
}