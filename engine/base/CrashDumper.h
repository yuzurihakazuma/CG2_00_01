#pragma once
#define NOMINMAX
#include <Windows.h>   // EXCEPTION_POINTERS, SYSTEMTIME など
#include <DbgHelp.h>   // MINIDUMP_TYPE


class CrashDumper{
public:
	// クラッシュダンプを生成する
	static void Install();

	// 任意のタイミングで手動ダンプを吐きたい場合（例外情報なしでも作れる）
	static bool WriteDumpNow();

private:
	// クラッシュダンプを生成する関数
	static LONG WINAPI ExportDump_(EXCEPTION_POINTERS* exception);
	// ダンプを書き出す関数
	static bool WriteDump_(EXCEPTION_POINTERS* exception);


};

