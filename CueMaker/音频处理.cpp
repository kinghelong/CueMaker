#include"framework.h"
#include"Macro.h"
#include <thread>
#include <vector>
#include <future>

wave_header g_wavHeader;                    // 存储解析后的WAV头信息

// 全局数组：存储左右声道的PCM振幅值（16位有符号整数）
std::vector<int16_t> g_leftPcmData;  // 左声道PCM数据
std::vector<int16_t> g_rightPcmData; // 右声道PCM数据
bool g_isWavLoaded = false;          // WAV加载标记
BYTE channels = 0;
int musicLengthInMs = 0, musicLengthInS = 0;             // 音乐总时长，单位毫秒
extern double g_zoomFactor ;    // 缩放倍率，1.0 是显示全长
extern double g_scrollOffset ;  // 当前画面左侧相对于起始点的偏移像素
extern int clickX, g_currentTimeMs, g_musicLengthMs ;
bool DrawTimeLine(HDC hdc, RECT rect, double musicLengthInS);


bool LoadWavFile(const std::wstring& wavPath)
{
    g_leftPcmData.clear();
    g_rightPcmData.clear();
    g_isWavLoaded = false;

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
    // [逻辑补充] 读取原始字节
    DWORD bytesToRead = g_wavHeader.data_size;
    std::vector<uint8_t> rawBuffer(bytesToRead);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, rawBuffer.data(), bytesToRead, &bytesRead, NULL) || bytesRead != bytesToRead) {
        CloseHandle(hFile); return false;
    }
    CloseHandle(hFile);

    int numChannels = g_wavHeader.channels;
    int bitsPerSample = g_wavHeader.bits_per_sample;
    int bytesPerSample = bitsPerSample / 8;
    int samplesPerChannel = (bytesRead / bytesPerSample) / numChannels;
    g_musicLengthMs = g_wavHeader.data_size / g_wavHeader.byterate;

    // 1. 核心优化：直接 Resize，消除 push_back 的重分配开销
    g_leftPcmData.assign(samplesPerChannel, 0);
    g_rightPcmData.assign(samplesPerChannel, 0);

    // 2. 准备并行解析
    int numThreads = std::thread::hardware_concurrency(); // 获取 CPU 核心数
    if (numThreads == 0) numThreads = 4;

    int chunkSize = samplesPerChannel / numThreads;
    std::vector<std::future<void>> futures;

    for (int t = 0; t < numThreads; ++t) {
        int startSample = t * chunkSize;
        int endSample = (t == numThreads - 1) ? samplesPerChannel : (t + 1) * chunkSize;

        // 拉姆达表达式多线程解析
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

    // 3. 等待所有线程完成
    for (auto& f : futures) f.wait();

    // 更新时长等信息
    musicLengthInS = static_cast<int>(g_wavHeader.data_size / g_wavHeader.byterate);
    musicLengthInMs = musicLengthInS * 1000;
    g_isWavLoaded = true;

    // 解析完后建议通知 UI 重绘
    // PostMessage(hMainWnd, WM_WAV_LOADED, 0, 0); 

    return true;
}



void DrawSingleWaveform(HDC hdc, RECT rect, const std::vector<int16_t>& data, COLORREF color) {
    if (data.empty()) return;

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    int centerY = rect.top + h / 2;
    double zoomedTotalWidth = w * g_zoomFactor;

    // 1. 计算当前可见的数据范围
    size_t startIdx = (size_t)((g_scrollOffset / zoomedTotalWidth) * data.size());
    size_t endIdx = (size_t)(((g_scrollOffset + w) / zoomedTotalWidth) * data.size());

    if (startIdx >= data.size()) {
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        return;
    }
    if (endIdx > data.size()) endIdx = data.size();

    // 2. 这里的 step 对应当前屏幕每像素跨越多少个采样点
    double samplesPerPixel = (double)(endIdx - startIdx) / w;

    for (int x = 0; x < w; x++) {
        size_t dataIdx = startIdx + (size_t)(x * samplesPerPixel);
        if (dataIdx >= data.size()) break;

        // 将 16 位采样值（-32768 ~ 32767）映射到半个矩形高度
        // 计算公式：(采样值 / 32768.0) * (总高度 / 2)
        int lineHeight = (int)((data[dataIdx] / 32768.0) * (h / 2.0));

        // 绘制折线
        if (x == 0)
            MoveToEx(hdc, rect.left + x, centerY - lineHeight, NULL);
        else
            LineTo(hdc, rect.left + x, centerY - lineHeight);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}
// 核心逻辑：计算画布区域分配
// 修改布局逻辑，预留底部空间
void CalculateFinalLayout(RECT clientRect, int numChannels, RECT& waveArea, RECT& timeArea, ChannelLayout& layout) {
    // 1. 底部固定留出 20 像素给时间轴
    timeArea = clientRect;
    timeArea.top = clientRect.bottom - 20;

    // 2. 剩余部分给波形
    waveArea = clientRect;
    waveArea.bottom = timeArea.top;

    layout.isStereo = (numChannels >= 2);
    if (!layout.isStereo) {
        layout.rectLeft = waveArea;
        SetRectEmpty(&layout.rectRight);
    }
    else {
        int waveHeight = waveArea.bottom - waveArea.top;
        int gapHeight = 10; // 双声道间的微小间隔
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

    // 1. 双缓冲准备
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 2. 背景填充
    HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(memDC, &rcTotal, blackBrush);

    // 3. 计算区域分配
    RECT waveArea, timeArea;
    ChannelLayout layout;
    CalculateFinalLayout(rcTotal, g_wavHeader.channels, waveArea, timeArea, layout);

    // 4. 绘制时间轴
    DrawTimeLine(memDC, timeArea, musicLengthInS);

    // 5. 绘制波形
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

    // 6. 绘制播放线（竖线）
    if (clickX >= 0) {
        // 将 clickX 转换成波形屏幕坐标（考虑缩放和滚动）
        double virtualX = (clickX / (double)width) * (width * g_zoomFactor);
        int screenX = (int)(virtualX - g_scrollOffset);

        if (screenX >= 0 && screenX <= width) {
            // 画播放线
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
            HPEN hOldPen = (HPEN)SelectObject(memDC, hPen);

            // 计算 X 坐标
            RECT rc;
            int playX = 0;
            GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            double zoomedTotalWidth = width * g_zoomFactor;
            if(g_currentTimeMs!=0)
                playX = (int)((g_currentTimeMs / (double)g_musicLengthMs) * zoomedTotalWidth - g_scrollOffset);
            else
                playX = clickX;

            MoveToEx(memDC, playX, 0, NULL);
            LineTo(memDC, playX, rc.bottom);

            SelectObject(memDC, hOldPen);
            DeleteObject(hPen);

        }
    }

    // 7. 最终拷贝到屏幕
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    // 8. 清理资源
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
    // ... 还可以更细
   
    for (int s = (int)startTime; s <= (int)endTime; s++) {
        if (s % finalInterval == 0) {
            // 计算 X：(时间占比 * 总像素宽) - 偏移量
            int x = (int)((s / musicLengthInS) * zoomedTotalWidth - g_scrollOffset);

            MoveToEx(hdc, x, rect.top, NULL);
            LineTo(hdc, x, rect.top + 5);

            // 时间文字 (稍微向上偏移一点防止出界)
            wchar_t timeStr[16];
            swprintf_s(timeStr, L"%02d:%02d", s / 60, s % 60);
            RECT textRect = { x - 25, rect.top + 5, x + 25, rect.bottom };
            DrawText(hdc, timeStr, -1, &textRect, DT_CENTER | DT_SINGLELINE | DT_TOP);
        }
    }
    return true;
}