#pragma once
#include "framework.h"
bool OpenAudioFileDialog(HWND, std::wstring&);
bool FileExists(const std::wstring&);
std::wstring ReplaceExt(const std::wstring& srcPath, std::wstring ext);

//void ResetAlbumInfo();
//void ResetTrackList();
//void ResetProgress();

