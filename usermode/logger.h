// language: C++17, file: logger.h, target: Windows 10/11 x64, MSVC
#pragma once
#include <string>
#include <map>
#include <fstream>
#include <mutex>

class Logger {
public:
    static Logger& Get();
    void Init(const std::string& dir);
    void Log(const std::string& event,
             const std::map<std::string, std::string>& fields = {});
private:
    Logger() = default;
    std::ofstream file_;
    std::mutex    mtx_;
    uintmax_t     bytes_{ 0 };
    std::string   GetTS() const;
    static constexpr uintmax_t MAX = 100ull * 1024 * 1024;
};
