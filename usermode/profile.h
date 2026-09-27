// language: C++17, file: profile.h, target: Windows 10/11 x64, MSVC
#pragma once
#include "recoil.h"
#include <map>
#include <string>
#include <vector>

class ProfileManager {
public:
    void AddDefaults();
    void LoadDir(const std::string& dir);
    bool Save(const std::string& dir, const WeaponProfile& p);

    const WeaponProfile* Get(const std::string& weapon) const;
    std::vector<std::string> List() const;

private:
    std::map<std::string, WeaponProfile> map_;
    static bool   Parse(const std::string& json, WeaponProfile& out);
    static std::string Serialize(const WeaponProfile& p);
    static std::string ExtractStr(const std::string& json, const std::string& key);
};
