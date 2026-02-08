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

// ===== 新增：存储点击位置对应的实际音频时间 =====
static double g_clickTimeRatio = -1.0;  // 点击位置对应的时间比例 (0.0 ~ 1.0)

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

    HANDLE hFile = CreateFile(wavPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
    if (!g_isWavLoaded) return;

    RECT rcTotal;
    GetClientRect(hWnd, &rcTotal);
    int width = rcTotal.right - rcTotal.left;
    int height = rcTotal.bottom - rcTotal.top;
    if (width <= 0 || height <= 0) return;

    // 双缓冲准备
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 背景填充
    HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(memDC, &rcTotal, blackBrush);

    // 计算区域分配
    RECT waveArea, timeArea;
    ChannelLayout layout;
    CalculateFinalLayout(rcTotal, g_wavHeader.channels, waveArea, timeArea, layout);

    // 绘制时间轴
    DrawTimeLine(memDC, timeArea, musicLengthInS);

    // 绘制波形
    DrawSingleWaveform(memDC, layout.rectLeft, g_leftPcmData, RGB(0, 255, 0));
    if (layout.isStereo) {
        DrawSingleWaveform(memDC, layout.rectRight, g_rightPcmData, RGB(255, 255, 0));

        // 分离带装饰线
        HPEN darkPen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
        HPEN oldPen = (HPEN)SelectObject(memDC, darkPen);
        int midY = layout.rectLeft.bottom + (layout.rectRight.top - layout.rectLeft.bottom) / 2;
        MoveToEx(memDC, 0, midY, NULL);
        LineTo(memDC, width, midY);
        SelectObject(memDC, oldPen);
        DeleteObject(darkPen);
    }

    // ===== 修复后的播放线绘制 =====
    if (g_clickTimeRatio >= 0.0)
    {
        // 计算播放线的屏幕 X 坐标
        // 公式：(时间比例 × 缩放后总宽度) - 滚动偏移
        double zoomedTotalWidth = width * g_zoomFactor;
        int playX = (int)(g_clickTimeRatio * zoomedTotalWidth - g_scrollOffset);

        // 只在可见范围内绘制
        if (playX >= 0 && playX <= width)
        {
            HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
            HPEN hOldPen = (HPEN)SelectObject(memDC, hPen);

            // 绘制竖线（从顶部到底部，不包括时间轴）
            MoveToEx(memDC, playX, 0, NULL);
            LineTo(memDC, playX, waveArea.bottom);

            SelectObject(memDC, hOldPen);
            DeleteObject(hPen);
        }
    }

    // 最终拷贝到屏幕
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    // 清理资源
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