// ZmqLogger.h
#ifndef OPENSHOT_LOGGER_H
#define OPENSHOT_LOGGER_H

#include <fstream>
#include <mutex>
#include <string>

namespace openshot {

	class ZmqLogger {
	private:
		std::recursive_mutex loggerMutex;
		std::string file_path;
		std::ofstream log_file;
		bool enabled;

		ZmqLogger() : enabled(false) {}
		ZmqLogger(ZmqLogger const&) = delete;
		ZmqLogger& operator=(ZmqLogger const&) = delete;

		static ZmqLogger* m_pInstance;

	public:
		static ZmqLogger* Instance();

		void AppendDebugMethod(
			std::string method_name,
			std::string arg1_name="", float arg1_value=-1.0,
			std::string arg2_name="", float arg2_value=-1.0,
			std::string arg3_name="", float arg3_value=-1.0,
			std::string arg4_name="", float arg4_value=-1.0,
			std::string arg5_name="", float arg5_value=-1.0,
			std::string arg6_name="", float arg6_value=-1.0
		);

		void Close();
		void Enable(bool is_enabled) { enabled = is_enabled; }
		void Path(std::string new_path);
		void Log(std::string message);
		void LogToFile(std::string message);
	};

}

#endif