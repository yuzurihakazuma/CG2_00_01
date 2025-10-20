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
	public:

		std::wstring ConvertString(const std::string& str);


		std::string ConvertString(const std::wstring& str);

		void Finalize();

		void Log(const std::string& message);
		void Log(const std::wstring& message);

		void Initialize();

		// シングルトンアクセス用（必要なら）
		static LogManager* GetInstance(){
			static LogManager instance;
			return &instance;
		}

	private:
		std::ofstream logStream_; // ログファイルのストリーム

	};

}// namespace logs

