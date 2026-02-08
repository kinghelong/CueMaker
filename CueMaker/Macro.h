#pragma once
#include"framework.h"
// 工具栏控件ID（唯一，避免冲突）
#define IDC_TOOLBAR_TOP             2001  // 顶部工具栏（文件操作：打开、保存、退出）
#define IDC_TOOLBAR_BOTTOM          2002  // 底部工具栏（CUE操作：添加、删除、清空）
#define IDC_MUTE_EDIT               2003  // 静音编辑按钮
#define IDC_VOLUME_SLIDER           2004  // 音量滑块
#define IDC_PROGRESS                2006  // 播放进度条
#define IDC_TIME_DISPLAY            2005  // 播放时间显示
#define IDC_STATIC_MUTE_TIP         2007 
#define IDC_STATIC_VOLUM_TIP        2008

// 工具栏按钮ID
#define ID_TOOL_TOP_OPEN            3001
#define ID_TOOL_TOP_SAVE            3002
#define ID_TOOL_TOP_EXIT            3003
#define ID_TOOL_BOTTOM_ADD          3004
#define ID_TOOL_BOTTOM_DEL          3005
#define ID_TOOL_BOTTOM_CLEAR        3006

#define MAX_LOADSTRING              100
#define WAVE_CTRL_CLASS             L"CoolEditWaveCtrlClass"
#define TOOLBAR_HEIGHT              48

#define ID_MUSIC_PRE                4000
#define ID_MUSIC_NEXT               4001
#define ID_MUSIC_PLAY               4002
#define ID_MUSIC_PAUSE              4003
#define ID_MUSIC_ADD                4004
#define ID_MUSIC_DEC                4005
#define ID_MUSIC_STOP               4006
#define ID_PLAY_PROGRESS            4007
#define ID_MUSIC_VOLUME             4008
#define ID_PLAY_TIME                4009
#define ID_TIMEZONE_START           4010
#define ID_TIMEZONE_END             4011


// Toolbar command IDs (3100 ~ 3199)
// ===== Toolbar IDs (Left → Right) =====

#define ID_TB_NEW_CUE         3100  // 新建 CUE
#define ID_TB_OPEN_CUE        3101  // 打开 CUE
#define ID_TB_EDIT_CUE        3102  // 编辑 CUE
#define ID_TB_ALBUM_INFO      3103  // 专辑信息

#define ID_TB_OPEN_AUDIO      3104  // 打开音频
#define ID_TB_FIND_SILENCE    3105  // 查找静音区
#define ID_TB_AUTO_GENERATE   3106  // 自动生成时间段

#define ID_TB_SPLIT_TRACKS    3107  // 分解成曲目
#define ID_TB_CONVERT_FORMAT  3108  // 转换格式

#define ID_TB_UNDO            3109  // 撤消
#define ID_TB_SETTINGS        3110  // 设置

