// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#endif // FRAMEWORK_H
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <cmath>
#include <vector>
#include <string>
#include <commapi.h>
#include <commctrl.h>
#include "Macro.h"
#include "All.h"
#include "MACDll.h"
#include "MACLib.h"
#include <Mmdeviceapi.h>
#include<map>
#include <Audioclient.h>
#include <mmeapi.h>

using std::vector;

#pragma comment(lib,"Comctl32.lib")

/***************************************************************************************
  MAC_DLL structure (仅存放APE库函数指针，必要定义，非业务封装)
***************************************************************************************/
struct MAC_DLL
{
	// APEDecompress functions
	proc_APEDecompress_CreateW            Create;
	proc_APEDecompress_Destroy           Destroy;
	proc_APEDecompress_GetData           GetData;
	proc_APEDecompress_Seek              Seek;
	proc_APEDecompress_GetInfo           GetInfo;
	// 多线程配置函数指针
	void(__stdcall* SetNumberOfThreads)(APE_DECOMPRESS_HANDLE hAPEDecompress, int nThreads);
};

struct wave_header
{
	char riff[4];                // "RIFF"
	unsigned int overall_size;   // 文件总大小-8字节
	char wave[4];                // "WAVE"
	char fmt_chunk_marker[4];    // "fmt "
	unsigned int length_of_fmt;  // 16 for PCM
	unsigned short format_type;  // PCM = 1
	unsigned short channels;     // 通道数
	unsigned int sample_rate;    // 采样率
	unsigned int byterate;       // 每秒字节数
	unsigned short block_align;  // 每采样点字节数
	unsigned short bits_per_sample; // 每样本位数
	char data_chunk_header[4];   // "data"
	unsigned int data_size;      // 数据大小
};
struct TrackInfo
{
	int index;
	std::wstring title;
};
struct AlbumInfo
{
	std::wstring albumName;
	std::wstring artistName;
	std::vector<TrackInfo> trackTitles;
};
struct LastSessionState
{
	AlbumInfo info;
	std::wstring audioFilePath;
};
struct WorkContext
{
	std::wstring audioPath;
	std::wstring cuePath;
	bool hasCueFile;   // 文件是否存在
};
struct ChannelLayout {
	RECT rectLeft;
	RECT rectRight;
	bool isStereo;
};
int read_wav_header(HANDLE hFile, wave_header* pWaveHeader);
int write_wav_header(HANDLE hFile, wave_header* pWaveHeader, int sampleRate, int channels, int dataBytes, int bitsPerSample);
LRESULT CALLBACK    WaveCtrlProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void                DrawDualChannelWaveform(HDC hdc, HWND hWnd); // 双声道波形绘制
std::wstring        FormatTime(int ms);
// ===== 新增：工具栏创建函数声明 =====
HWND                CreateToolbarTop(HWND hWnd, HINSTANCE hInstance);
HWND                CreateToolbarBottom(HWND hWnd, HINSTANCE hInstance);

//ini和对话框相关
void GetExeDirectory(TCHAR* szDir, DWORD dwSize);
void ShowAlbumInfoDialog(HWND hWndParent);// 显示专辑信息对话框
bool OnNewCue(HWND hWnd);// 新建CUE文件对话框
std::wstring ReplaceExt(const std::wstring& srcPath, std::wstring ext);// 替换文件扩展名
bool saveWorkStateToIni(const wchar_t* iniPath, const wchar_t* audioFilePath, const wchar_t* albumIniFile, const wchar_t* cueFile, int workCompleted);// 保存工作状态到INI文件	
bool readWorkStateFromIni(const wchar_t* iniPath, std::wstring& outAudioFile, std::wstring& outAlbumIniFile, std::wstring& outCueFile, int& outWorkCompleted);// 从INI文件读取工作状态
bool ReadIniFile(const std::wstring& filePath, std::map<std::wstring, std::wstring>& outData);


//音频处理相关
int APE_Decompress_WAV(MAC_DLL MACDll, HMODULE hMACDll, HWND hWnd, WCHAR* pFilename, APE_DECOMPRESS_HANDLE hAPEDecompress);
int GetWaveOrPcm(HWND hWnd, std::wstring fileName, int mode);
bool LoadWavFile(const std::wstring& wavFilePath);
int read_wav_header(HANDLE hFile, wave_header* pWaveHeader);


//cue文件读写相关
AlbumInfo GetAlbumInfoFromIni();
bool GenerateCueFile();

//播放音乐
unsigned int __stdcall PlayAudioThread(void* pParam);
void StartPlay(const wchar_t* fileName);
void PausePlay();
void SeekPlay(double sec, WAVEFORMATEX* wf = nullptr);
void StopPlay();

//播放进度ui更新
void CalculateFinalLayout(RECT clientRect, int numChannels, RECT& waveArea, RECT& timeArea, ChannelLayout& layout);
