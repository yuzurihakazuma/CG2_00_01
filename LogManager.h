#pragma once
#include <string>
#include <format>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <d3d12.h>
#include <wrl.h>
#include <Windows.h>

namespace logs{

	class LogManager{


		std::wstring ConvertString(const std::string& str);

		std::string ConvertString(const std::wstring& str);

		// ログファイルを書き出す
		void Log(std::ostream& os, const std::string& message);

		void Instantiate();

		// シングルトンアクセス用（必要なら）
		static LogManager* GetInstance(){
			static LogManager instance;
			return &instance;
		}

	private:
		std::ofstream logStream; // ログファイルのストリーム

	};

}// namespace logs

