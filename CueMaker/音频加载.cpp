#include"framework.h"

int write_wav_header(HANDLE hFile, wave_header* pWaveHeader, int sampleRate, int channels, int dataBytes, int bitsPerSample)
{    if (hFile == INVALID_HANDLE_VALUE || pWaveHeader == NULL)
        return -1;

    // 清零，避免脏字段
    ZeroMemory(pWaveHeader, sizeof(wave_header));

    memcpy(pWaveHeader->riff, "RIFF", 4);
    memcpy(pWaveHeader->wave, "WAVE", 4);
    memcpy(pWaveHeader->fmt_chunk_marker, "fmt ", 4);
    memcpy(pWaveHeader->data_chunk_header, "data", 4);

    pWaveHeader->length_of_fmt = 16;
    pWaveHeader->format_type = 1; // PCM
    pWaveHeader->channels = (WORD)channels;
    pWaveHeader->sample_rate = sampleRate;
    pWaveHeader->bits_per_sample = (WORD)bitsPerSample;
    pWaveHeader->block_align = (WORD)(channels * bitsPerSample / 8);
    pWaveHeader->byterate = sampleRate * pWaveHeader->block_align;

    pWaveHeader->data_size = dataBytes;
    pWaveHeader->overall_size = dataBytes + sizeof(wave_header) - 8;

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    DWORD written = 0;
    if (!WriteFile(hFile, pWaveHeader, sizeof(wave_header), &written, NULL))
        return -2;

    if (written != sizeof(wave_header))
        return -3;

    return 0;
}
int read_wav_header(HANDLE hFile, wave_header* pWaveHeader)
{
    if (hFile == INVALID_HANDLE_VALUE || pWaveHeader == NULL)
        return -1;
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD readBytes = 0;
    if (!ReadFile(hFile, pWaveHeader, sizeof(wave_header), &readBytes, NULL))
        return -2;
    if (readBytes != sizeof(wave_header))
        return -3;
    // 简单验证WAV头合法性
    if (memcmp(pWaveHeader->riff, "RIFF", 4) != 0 ||
        memcmp(pWaveHeader->wave, "WAVE", 4) != 0 ||
        memcmp(pWaveHeader->fmt_chunk_marker, "fmt ", 4) != 0 ||
        memcmp(pWaveHeader->data_chunk_header, "data", 4) != 0)
    {
        return -4; // 非法WAV文件
    }
    return 0;
}
std::wstring FormatTime(int ms)
{
    int minutes = (ms / 1000) / 60;
    int seconds = (ms / 1000) % 60;
    int frac = ms % 1000;
    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d.%03d", minutes, seconds, frac);
    return std::wstring(buf);
}