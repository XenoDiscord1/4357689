// language: C++17, file: logger.cpp, target: Windows 10/11 x64, MSVC
#include "logger.h"
#include <windows.h>
#include <sstream>
#include <filesystem>

Logger& Logger::Get() { static Logger i; return i; }

void Logger::Init(const std::string& dir) {
    std::filesystem::create_directories(dir);
    SYSTEMTIME st{}; GetLocalTime(&st);
    char f[80];
    snprintf(f, sizeof(f), "session_%04d%02d%02d_%02d%02d%02d.jsonl",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    file_.open(dir + "\\" + f, std::ios::app | std::ios::binary);
    bytes_ = 0;
}

std::string Logger::GetTS() const {
    SYSTEMTIME st{}; GetSystemTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

void Logger::Log(const std::string& ev,
                 const std::map<std::string, std::string>& fields)
{
    if (!file_.is_open() || bytes_ >= MAX) return;
    std::lock_guard<std::mutex> lk(mtx_);
    std::ostringstream o;
    o << R"({"timestamp":")" << GetTS() << R"(","event":")" << ev << '"';
    for (const auto& [k, v] : fields)
        o << ",\"" << k << "\":\"" << v << '"';
    o << "}\n";
    std::string line = o.str();
    file_.write(line.data(), (std::streamsize)line.size());
    file_.flush();
    bytes_ += line.size();
}
