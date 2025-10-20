#define NOMINMAX
#include "CrashDumper.h"
#include <strsafe.h>

#pragma comment(lib, "Dbghelp.lib")

void CrashDumper::Install(){
	// 何度呼ばれても最後の登録が有効になるだけ
	SetUnhandledExceptionFilter(&CrashDumper::ExportDump_);
}

bool CrashDumper::WriteDumpNow(){
	// 例外情報なしでダンプを書き出す
	return WriteDump_(nullptr);

}

LONG __stdcall CrashDumper::ExportDump_(EXCEPTION_POINTERS* exception){
	WriteDump_(exception);
	// 既定の処理へ
	return EXCEPTION_EXECUTE_HANDLER;

}

bool CrashDumper::WriteDump_(EXCEPTION_POINTERS* exception){
	// 時刻を取得し、出力先フォルダを作成
	SYSTEMTIME time {};
	GetLocalTime(&time);
	// フォルダを作成
	CreateDirectory(L"./Dumps", nullptr);
	// ファイル名を作成
	wchar_t filePath[MAX_PATH] = { 0 };
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	// processId(このexeのId)とクラッシュ(例外)の発生したthreadIdを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation { 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	// 他に関連づけられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;


}
