#include"framework.h"
#include<map>
#include<vector>
#include<string>
#include"resource.h"  
#include <strsafe.h>
#include <algorithm>

bool ReadIniFile(const std::wstring& filePath, std::map<std::wstring, std::wstring>& outData)
{
	WCHAR buffer[256];
	DWORD charsRead = GetPrivateProfileSectionNamesW(buffer, sizeof(buffer) / sizeof(WCHAR), filePath.c_str());
	if (charsRead == 0)
	{
		return false; // 读取失败或文件不存在
	}
	WCHAR* section = buffer;
	while (*section)
	{
		WCHAR keyBuffer[256];
		DWORD keysRead = GetPrivateProfileStringW(section, nullptr, L"", keyBuffer, sizeof(keyBuffer) / sizeof(WCHAR), filePath.c_str());
		if (keysRead > 0)
		{
			WCHAR* key = keyBuffer;
			while (*key)
			{
				WCHAR valueBuffer[256];
				GetPrivateProfileStringW(section, key, L"", valueBuffer, sizeof(valueBuffer) / sizeof(WCHAR), filePath.c_str());
				outData[std::wstring(section) + L"." + std::wstring(key)] = std::wstring(valueBuffer);
				key += wcslen(key) + 1;
			}
		}
		section += wcslen(section) + 1;
	}
	return true;
}	
bool WriteIniFile(const std::wstring& filePath, const std::map<std::wstring, std::wstring>& inData)
{
	std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> sections;
	for (const auto& kv : inData)
	{
		size_t dotPos = kv.first.find(L'.');
		if (dotPos != std::wstring::npos)
		{
			std::wstring section = kv.first.substr(0, dotPos);
			std::wstring key = kv.first.substr(dotPos + 1);
			sections[section].emplace_back(key, kv.second);
		}
	}
	for (const auto& sec : sections)
	{
		for (const auto& kv : sec.second)
		{
			WritePrivateProfileStringW(sec.first.c_str(), kv.first.c_str(), kv.second.c_str(), filePath.c_str());
		}
	}
	return true;
}
bool DeleteIniFileSection(const std::wstring& filePath, const std::wstring& section)
{
	return WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, filePath.c_str()) != 0;
}
bool DeleteIniFileKey(const std::wstring& filePath, const std::wstring& section, const std::wstring& key)
{
	return WritePrivateProfileStringW(section.c_str(), key.c_str(), nullptr, filePath.c_str()) != 0;
}
bool IniFileSectionExists(const std::wstring& filePath, const std::wstring& section)
{
	WCHAR buffer[256];
	DWORD charsRead = GetPrivateProfileSectionNamesW(buffer, sizeof(buffer) / sizeof(WCHAR), filePath.c_str());
	if (charsRead == 0)
	{
		return false; // 读取失败或文件不存在
	}
	WCHAR* sec = buffer;
	while (*sec)
	{
		if (section == sec)
		{
			return true;
		}
		sec += wcslen(sec) + 1;
	}
	return false;
}
bool IniFileKeyExists(const std::wstring& filePath, const std::wstring& section, const std::wstring& key)
{
	WCHAR buffer[256];
	DWORD keysRead = GetPrivateProfileStringW(section.c_str(), nullptr, L"", buffer, sizeof(buffer) / sizeof(WCHAR), filePath.c_str());
	if (keysRead == 0)
	{
		return false; // 读取失败或节不存在
	}
	WCHAR* k = buffer;
	while (*k)
	{
		if (key == k)
		{
			return true;
		}
		k += wcslen(k) + 1;
	}
	return false;
}
bool ClearIniFile(const std::wstring& filePath)
{
	return WritePrivateProfileStringW(nullptr, nullptr, nullptr, filePath.c_str()) != 0;
}
bool GetIniFileSections(const std::wstring& filePath, std::vector<std::wstring>& outSections)
{
	WCHAR buffer[1024];
	DWORD charsRead = GetPrivateProfileSectionNamesW(buffer, sizeof(buffer) / sizeof(WCHAR), filePath.c_str());
	if (charsRead == 0)
	{
		return false; // 读取失败或文件不存在
	}
	WCHAR* section = buffer;
	while (*section)
	{
		outSections.push_back(std::wstring(section));
		section += wcslen(section) + 1;
	}
	return true;
}
bool GetIniFileKeys(const std::wstring& filePath, const std::wstring& section, std::vector<std::wstring>& outKeys)
{
	WCHAR buffer[1024];
	DWORD keysRead = GetPrivateProfileStringW(section.c_str(), nullptr, L"", buffer, sizeof(buffer) / sizeof(WCHAR), filePath.c_str());
	if (keysRead == 0)
	{
		return false; // 读取失败或节不存在
	}
	WCHAR* key = buffer;
	while (*key)
	{
		outKeys.push_back(std::wstring(key));
		key += wcslen(key) + 1;
	}
	return true;
}
bool GetIniFileKeyValue(const std::wstring& filePath, const std::wstring& section, const std::wstring& key, std::wstring& outValue)
{
	WCHAR valueBuffer[256];
	DWORD charsRead = GetPrivateProfileStringW(section.c_str(), key.c_str(), L"", valueBuffer, sizeof(valueBuffer) / sizeof(WCHAR), filePath.c_str());
	if (charsRead == 0)
	{
		return false; // 读取失败或节/键不存在
	}
	outValue = std::wstring(valueBuffer);
	return true;
}
bool SetIniFileKeyValue(const std::wstring& filePath, const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
	return WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), filePath.c_str()) != 0;
}


bool saveWorkStateToIni(const wchar_t* iniPath, const wchar_t* audioFilePath, const wchar_t* albumIniFile, const wchar_t* cueFile, int workCompleted)
{
	if (!iniPath) return false;
	bool ok = true;
	if(audioFilePath)
		ok &= WritePrivateProfileStringW(L"LastSession", L"LastAudioFile", audioFilePath ? audioFilePath : L"", iniPath);
	if(albumIniFile)
		ok &= WritePrivateProfileStringW(L"LastSession", L"LastAlbumIni", albumIniFile ? albumIniFile : L"", iniPath);
	if(cueFile)
		ok &= WritePrivateProfileStringW(L"LastSession", L"LastCueFile", cueFile ? cueFile : L"", iniPath);
	std::wstring completed = std::to_wstring(workCompleted);
	ok &= WritePrivateProfileStringW(L"LastSession", L"WorkCompleted", completed.c_str(), iniPath);
	return ok;
}
bool readWorkStateFromIni(const wchar_t* iniPath, std::wstring& outAudioFile, std::wstring& outAlbumIniFile, std::wstring& outCueFile, int& outWorkCompleted)
{
	if (!iniPath) return false;
	WCHAR buffer[512];
	DWORD read;
	// LastAudioFile
	read = GetPrivateProfileStringW(L"LastSession", L"LastAudioFile", L"", buffer, sizeof(buffer) / sizeof(WCHAR), iniPath);
	outAudioFile = std::wstring(buffer, read);
	// LastAlbumIni
	read = GetPrivateProfileStringW(L"LastSession", L"LastAlbumIni", L"", buffer, sizeof(buffer) / sizeof(WCHAR), iniPath);
	outAlbumIniFile = std::wstring(buffer, read);
	// LastCueFile
	read = GetPrivateProfileStringW(L"LastSession", L"LastCueFile", L"", buffer, sizeof(buffer) / sizeof(WCHAR), iniPath);
	outCueFile = std::wstring(buffer, read);
	// WorkCompleted
	read = GetPrivateProfileStringW(L"LastSession", L"WorkCompleted", L"0", buffer, sizeof(buffer) / sizeof(WCHAR), iniPath);
	outWorkCompleted = _wtoi(buffer);
	return true;
}

int workStationIni(HWND hWnd, std::vector<std::wstring>& albumIniList)
{
	wchar_t exeDir[MAX_PATH];
	GetExeDirectory(exeDir, MAX_PATH);
	std::wstring searchPath = exeDir;
	if (searchPath.back() != L'\\')
		searchPath += L'\\';
	searchPath += L"*.ini"; 

	WIN32_FIND_DATAW ffd{};
	HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		wchar_t msg[256];
		swprintf_s(msg, L"FindFirstFile failed (%lu)", GetLastError());
		MessageBoxW(hWnd, msg, L"Error", MB_OK);
		return 0;
	}
	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		if (_wcsicmp(ffd.cFileName, L"CueMakerState.ini") == 0)
			continue;

		albumIniList.push_back(ffd.cFileName);

	} while (FindNextFileW(hFind, &ffd));

	FindClose(hFind);
	return (int)albumIniList.size();
}

int readIniFromAlbumToTrackList(WCHAR* iniName, HWND hTrackList)
{
	// 1. 获取 exe 目录
	wchar_t exeDir[MAX_PATH] = { 0 };
	GetExeDirectory(exeDir, MAX_PATH);

	// 安全拼接路径
	HRESULT hr = StringCchCat(exeDir, MAX_PATH, iniName);
	if (FAILED(hr)) {
		return 0; // 路径拼接失败
	}

	// 2. 读取 ini 文件
	std::map<std::wstring, std::wstring> iniData;
	if (!ReadIniFile(exeDir, iniData)) {
		return 0; // 读取失败
	}

	// 3. 收集 TrackInfo
	std::vector<TrackInfo> tracks;
	for (const auto& pair : iniData)
	{
		const std::wstring& key = pair.first;

		// 筛选以 "Track_" 开头且以 ".Title" 结尾
		if (key.rfind(L".Title") == key.size() - 6 && key.find(L"Track_") == 0)
		{
			std::wstring sectionPart = key.substr(0, key.size() - 6); // 去掉 ".Title"

			// 找下划线位置，提取编号
			size_t underscorePos = sectionPart.find(L"_");
			if (underscorePos == std::wstring::npos) continue;

			std::wstring indexStr = sectionPart.substr(underscorePos + 1);

			// 查找对应 ID，如果没有则跳过
			std::wstring idKey = sectionPart + L".ID";
			if (iniData.find(idKey) == iniData.end()) continue;

			// 转换编号为 int
			int trackIndex = 0;
			try {
				trackIndex = std::stoi(indexStr);
			}
			catch (const std::exception&) {
				continue; // 转换失败跳过
			}

			// 保存到 vector
			TrackInfo track;
			track.index = trackIndex;
			track.title = pair.second;
			tracks.push_back(track);
		}
	}

	// 4. 按 trackIndex 排序
	std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
		return a.index < b.index;
		});

	// 5. 插入 ListBox
	for (const auto& track : tracks) {
		SendMessage(hTrackList, LB_ADDSTRING, 0, (LPARAM)track.title.c_str());
	}

	return 1;
}