// language: C++17, file: profile.cpp, target: Windows 10/11 x64, MSVC
#include "profile.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>

void ProfileManager::AddDefaults() {
    auto ak = MakeAK47Profile(); map_[ak.weapon] = ak;
    auto m4 = MakeM4A1Profile(); map_[m4.weapon] = m4;
    auto aw = MakeAWPProfile();  map_[aw.weapon] = aw;
}

const WeaponProfile* ProfileManager::Get(const std::string& w) const {
    auto it = map_.find(w);
    return it != map_.end() ? &it->second : nullptr;
}

std::vector<std::string> ProfileManager::List() const {
    std::vector<std::string> v;
    for (const auto& [k, _] : map_) v.push_back(k);
    return v;
}

void ProfileManager::LoadDir(const std::string& dir) {
    if (!std::filesystem::exists(dir)) return;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != ".json") continue;
        std::ifstream f(e.path());
        std::string json((std::istreambuf_iterator<char>(f)), {});
        WeaponProfile p;
        if (Parse(json, p)) map_[p.weapon] = p;
    }
}

bool ProfileManager::Save(const std::string& dir, const WeaponProfile& p) {
    std::filesystem::create_directories(dir);
    std::ofstream f(dir + "\\" + p.weapon + ".json");
    if (!f) return false;
    f << Serialize(p);
    return true;
}

std::string ProfileManager::ExtractStr(const std::string& json, const std::string& key) {
    std::regex rx("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch m;
    return std::regex_search(json, m, rx) ? m[1].str() : "";
}

bool ProfileManager::Parse(const std::string& json, WeaponProfile& out) {
    out.weapon          = ExtractStr(json, "weapon");
    out.calibrated_date = ExtractStr(json, "calibrated_date");
    out.spray_pattern   = ExtractStr(json, "spray_pattern");
    if (out.weapon.empty()) return false;

    std::regex rx(R"(\{[^}]*"x"\s*:\s*(-?\d+)[^}]*"y"\s*:\s*(-?\d+)[^}]*"delay"\s*:\s*(\d+)[^}]*\})");
    for (auto it = std::sregex_iterator(json.begin(), json.end(), rx);
         it != std::sregex_iterator{}; ++it)
    {
        out.offsets.push_back({
            std::stoi((*it)[1].str()),
            std::stoi((*it)[2].str()),
            std::stoi((*it)[3].str())
        });
    }
    return !out.offsets.empty();
}

std::string ProfileManager::Serialize(const WeaponProfile& p) {
    std::ostringstream o;
    o << "{\n  \"weapon\": \""          << p.weapon          << "\",\n"
      << "  \"calibrated_date\": \""    << p.calibrated_date << "\",\n"
      << "  \"spray_pattern\": \""      << p.spray_pattern   << "\",\n"
      << "  \"offsets\": [\n";
    for (size_t i = 0; i < p.offsets.size(); ++i) {
        const auto& off = p.offsets[i];
        o << "    {\"x\": " << off.x << ", \"y\": " << off.y
          << ", \"delay\": " << off.delay_ms << "}";
        if (i + 1 < p.offsets.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}
