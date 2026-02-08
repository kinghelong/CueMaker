#include"framework.h"
#include <map>

extern std::vector<int16_t> g_leftPcmData;  // 左声道PCM数据
extern std::vector<int16_t> g_rightPcmData; // 右声道PCM数据
wchar_t audioFilePath[MAX_PATH] = { 0 };

AlbumInfo GetAlbumInfoFromIni()
{
    AlbumInfo info; // 初始化返回值，默认字段均为空

    TCHAR szExeDir[512] = { 0 };
    GetExeDirectory(szExeDir, 512);
    std::wstring cueMakerIniPath = std::wstring(szExeDir) + L"CueMakerState.ini";
    wchar_t buf[MAX_PATH] = { 0 };

    // 1. 读取实际的专辑INI文件路径（存入buf）
    GetPrivateProfileStringW(L"LastSession", L"LastAlbumIni", L"", buf, MAX_PATH, cueMakerIniPath.c_str());
    GetPrivateProfileStringW(L"LastSession", L"LastAudioFile", L"", audioFilePath, MAX_PATH, cueMakerIniPath.c_str());

    if (wcslen(buf) == 0) // 判断是否获取到有效路径
    {
        return info; // 未获取到路径，返回空的AlbumInfo
    }
    std::wstring iniFilePath = buf; // 替换硬编码路径，使用实际读取到的路径

    // 2. 定义map接收INI数据，调用ReadIniFile读取
    std::map<std::wstring, std::wstring> iniData;
    bool readSuccess = ReadIniFile(iniFilePath, iniData);
    if (!readSuccess) // 读取失败，返回空的AlbumInfo
    {
        return info;
    }

    // 3. 提取专辑名和艺术家名（使用find避免map插入无效空值）
    auto albumNameIt = iniData.find(L"CUE_Album.AlbumName");
    if (albumNameIt != iniData.end())
    {
        info.albumName = albumNameIt->second;
    }

    auto artistNameIt = iniData.find(L"CUE_Album.Artist");
    if (artistNameIt != iniData.end())
    {
        info.artistName = artistNameIt->second;
    }

    // 4. 提取所有曲目信息（遍历iniData，筛选Track_xx相关数据）
    for (const auto& pair : iniData)
    {
        const std::wstring& mapKey = pair.first;
        // 筛选以"Track_"开头且以".Title"结尾的键（确保是曲目标题）
        size_t trackPrefixPos = mapKey.find(L"Track_");
        size_t titleSuffixPos = mapKey.rfind(L".Title");
        if (trackPrefixPos == 0 && titleSuffixPos != std::wstring::npos)
        {
            // 提取曲目编号部分（如"Track_01.Title" → 提取"01"）
            std::wstring sectionPart = mapKey.substr(0, titleSuffixPos);
            size_t numStartPos = sectionPart.find(L"_") + 1;
            std::wstring indexStr = sectionPart.substr(numStartPos);

            // 提取曲目ID（对应Track_xx.ID）
            std::wstring idKey = sectionPart + L".ID";
            auto idIt = iniData.find(idKey);
            if (idIt == iniData.end())
            {
                continue; // 无ID信息，跳过该曲目
            }

            // 转换编号为int类型
            int trackIndex = 0;
            try
            {
                trackIndex = std::stoi(indexStr); // 宽字符串转int（C++11及以上支持）
            }
            catch (...)
            {
                continue; // 转换失败，跳过该曲目
            }

            // 构建TrackInfo并加入向量
            TrackInfo track;
            track.index = trackIndex;
            track.title = pair.second; // pair.second就是曲目标题值
            info.trackTitles.push_back(track);
        }
    }
    return info;
}
struct MuteRange
{
    INT64 start;
    INT64 end;
};

std::vector<MuteRange> GetMuteZones()
{
    std::vector<MuteRange> zones;
    if (g_leftPcmData.empty()) return zones;

    const int SILENCE_THRESHOLD = 150;      // 阈值没问题
    const int MIN_SILENCE_LEN = 44100;      // 至少1秒静音才算间歇
    const int MIN_SOUND_CONFIRM = 1000;     // 只要有约0.02秒声音，就判定音乐开始

    size_t sampleCount = min(g_leftPcmData.size(), g_rightPcmData.size());

    bool inSilence = false;
    INT64 silenceStart = 0;
    int silenceCount = 0;
    int soundCount = 0;

    for (size_t i = 0; i < sampleCount; ++i)
    {
        // 优化：先算左声道，如果超了就不用算右声道的 abs 了
        bool isSilent = (abs(g_leftPcmData[i]) < SILENCE_THRESHOLD &&
            abs(g_rightPcmData[i]) < SILENCE_THRESHOLD);

        if (isSilent)
        {
            silenceCount++;
            soundCount = 0;

            if (!inSilence && silenceCount >= MIN_SILENCE_LEN)
            {
                inSilence = true;
                // 回溯到刚开始静音的那一刻
                silenceStart = i - MIN_SILENCE_LEN;
            }
        }
        else
        {
            soundCount++;
            // 注意：不要在这里重置 silenceCount，只有确定是音乐开始了才重置

            if (inSilence && soundCount >= MIN_SOUND_CONFIRM)
            {
                // 音乐确实开始了
                INT64 silenceEnd = i - MIN_SOUND_CONFIRM;
                zones.push_back({ silenceStart, silenceEnd });
                inSilence = false;
                silenceCount = 0;
            }

            if (!inSilence) silenceCount = 0;
        }
    }

    // 如果文件末尾正好是静音，也补上
    if (inSilence) {
        zones.push_back({ silenceStart, (INT64)sampleCount - 1 });
    }

    return zones;
}

std::wstring SamplesToCueTime(INT64 samples, int sampleRate = 44100)
{
    // 计算总秒数和剩余采样点
    double totalSeconds = (double)samples / sampleRate;
    int minutes = (int)(totalSeconds / 60);
    int seconds = (int)totalSeconds % 60;
    // 剩余不满1秒的部分转为帧 (0-74帧)
    int frames = (int)((totalSeconds - (int)totalSeconds) * 75);

    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d:%02d", minutes, seconds, frames);
    return buf;
}



std::wstring GenerateCueContent(const std::wstring& wavFileName)
{
    AlbumInfo info = GetAlbumInfoFromIni();
    std::vector<MuteRange> mutes = GetMuteZones();

    // 1. 收集所有潜在的分轨点（剔除首尾静音区，只保留中间有效分轨静音）
    std::vector<INT64> trackPoints;
    trackPoints.push_back(0); // 必然的 TRACK 01 起点

    // 核心修改：跳过第一个（起始静音）和最后一个（尾声静音）静音区，只处理中间的
    if (mutes.size() >= 2) // 至少有2个静音区才需要剔除首尾，否则无中间有效静音区
    {
        // 遍历范围：从第2个静音区（index=1）到倒数第2个静音区（index=mutes.size()-2）
        for (size_t m_idx = 1; m_idx <= mutes.size() - 2; ++m_idx)
        {
            const auto& m = mutes[m_idx];
            // 过滤极其靠前的静音（防止漏判，保留原有过滤逻辑）
            if (m.end < 44100) continue;
            trackPoints.push_back(m.end);
        }
    }
    // 若静音区不足2个（无首尾可剔），则不添加任何额外分轨点，仅保留初始的0

    size_t albumTrackCount = info.trackTitles.size();

    // 如果 INI 读取失败或没歌，才 fallback 到检测出来的数量
    if (albumTrackCount == 0) albumTrackCount = trackPoints.size();

    std::wstring cue = L"";
    cue.reserve(4096);
    cue += L"REM Generated by Gemini CueMaker\r\n";
    // 加上非空判定
    cue += L"PERFORMER \"" + (info.artistName.empty() ? L"Unknown Artist" : info.artistName) + L"\"\r\n";
    cue += L"TITLE \"" + (info.albumName.empty() ? L"Unknown Album" : info.albumName) + L"\"\r\n";
    cue += L"FILE \"" + wavFileName + L"\" WAVE\r\n";

    // 删掉冗余的 trackPoints.push_back(0); 避免无效分轨点

    // 3. 只循环 albumTrackCount 次
    for (size_t i = 0; i < albumTrackCount; ++i) {
        int trackNum = (int)i + 1;

        // 【关键】现在的 trackPoints[0] 已经是第一首歌响起来的那个点了
        INT64 currentPoint = (i < trackPoints.size()) ? trackPoints[i] : trackPoints.back();

        // 查找标题
        std::wstring title = L"Track " + std::to_wstring(trackNum);
        for (const auto& t : info.trackTitles) {
            if (t.index == trackNum) {
                title = t.title;
                break;
            }
        }

        wchar_t buf[128];
        swprintf_s(buf, L"  TRACK %02d AUDIO\r\n", trackNum);
        cue += buf;
        cue += L"    TITLE \"" + title + L"\"\r\n";
        cue += L"    INDEX 01 " + SamplesToCueTime(currentPoint) + L"\r\n";
    }

    return cue;
}

void SaveCueFile(const std::wstring& cuePath, const std::wstring& content)
{
    // 将宽字符转为多字节 (通常 CUE 使用 ANSI/Local 代码页)
    int size_needed = WideCharToMultiByte(CP_ACP, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_ACP, 0, content.c_str(), -1, &str[0], size_needed, NULL, NULL);

    HANDLE hFile = CreateFile(cuePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, str.c_str(), (DWORD)strlen(str.c_str()), &written, NULL);
        CloseHandle(hFile);
    }
}

// 建议在外部先检查音频路径是否有效
bool GenerateCueFile()
{
    // 1. 必须先调这个！它会填充全局变量 audioFilePath，并返回 info
    AlbumInfo info = GetAlbumInfoFromIni();
	if (info.albumName.empty() && info.artistName.empty() && info.trackTitles.empty())  
    {
        MessageBox(NULL, L"专辑信息为空，", L"错误", MB_OK);
        return false;
    }

    // 2. 安全检查：确保路径真的读到了
    if (wcslen(audioFilePath) == 0) {
        MessageBox(NULL, L"无法从INI读取音频路径，请检查CueMakerState.ini", L"错误", MB_OK);
        return false;
    }

    // 3. 提取纯文件名（WAV），CUE通常不建议带绝对路径
    std::wstring fullPath = audioFilePath;
    std::wstring wavNameOnly = fullPath;
    size_t lastSlash = fullPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        wavNameOnly = fullPath.substr(lastSlash + 1);
    }

    // 4. 传入文件名生成内容
    std::wstring cueContent = GenerateCueContent(wavNameOnly);

    // 5. 生成保存路径
    std::wstring cueSavePath = ReplaceExt(fullPath, L".cue");

    SaveCueFile(cueSavePath, cueContent);
    return true;
}