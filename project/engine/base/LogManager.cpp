#include "LogManager.h"


	void logs::LogManager::Initialize(){

		// ログのディレクトリを用意
		std::filesystem::create_directory("logs");
		// 現在時刻を取得（UTC時刻）
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
		std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
			nowSeconds = std::chrono::time_point_cast< std::chrono::seconds >( now );
		// 日本時間（PCの設定時間）に変換
		std::chrono::zoned_time localTime(std::chrono::current_zone(), nowSeconds);
		// formatを使って年月日_時分秒の文字列に変換
		std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
		//時刻を使ってファイル名を決定
		std::string logFilePath = std::string("logs/") + dateString + ".log";
		// ファイルを作って書き込み準備
		logStream_.open(logFilePath); // ← メンバ変数で開く
	}

	void logs::LogManager::Finalize(){
		if ( logStream_.is_open() ) {
			logStream_.close();
		}
	}
	std::wstring logs::LogManager::ConvertString(const std::string& str){
		if ( str.empty() ) return std::wstring();
		int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), ( int ) str.size(), NULL, 0);
		if ( sizeNeeded == 0 ) return std::wstring();
		std::wstring result(sizeNeeded, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), ( int ) str.size(), &result[0], sizeNeeded);
		return result;
	}

	std::string logs::LogManager::ConvertString(const std::wstring& str){
		if ( str.empty() ) return std::string();
		int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), ( int ) str.size(), NULL, 0, NULL, NULL);
		if ( sizeNeeded == 0 ) return std::string();
		std::string result(sizeNeeded, 0);
		WideCharToMultiByte(CP_UTF8, 0, str.data(), ( int ) str.size(), &result[0], sizeNeeded, NULL, NULL);
		return result;
	}



	// std::string用
	void logs::LogManager::Log(const std::string& message){
		if ( logStream_.is_open() ) {
			logStream_ << message << std::endl;
		}
		OutputDebugStringA(message.c_str());
	}

	// std::wstring用
	void logs::LogManager::Log(const std::wstring& message){
		// 必要ならUTF-8変換してファイルに書き込む
		std::string converted = ConvertString(message); // UTF-8変換
		if ( logStream_.is_open() ) {
			logStream_ << converted << std::endl;
		}
		OutputDebugStringW(message.c_str());
	}
