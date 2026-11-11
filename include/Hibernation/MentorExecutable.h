#pragma once
#include <filesystem>
#include <algorithm>
#include <cwctype>

namespace hibernation {
// Only recover versioned files owned by the official desktop installation.
// A missing custom executable remains an error rather than changing providers.
inline std::filesystem::path resolveMentorExecutable(const std::filesystem::path& configured,
                                                     const std::filesystem::path& managedRoot){
    namespace fs=std::filesystem;
    std::error_code ec;
    if(!configured.empty()&&configured.is_absolute()&&configured.extension()==L".exe"&&fs::is_regular_file(configured,ec))return configured;
    const auto folded=[](const fs::path& p){auto s=p.lexically_normal().native();std::transform(s.begin(),s.end(),s.begin(),[](auto c){return std::towlower(c);});return s;};
    if(!configured.empty()&&(configured.filename()!=L"codex.exe"||folded(configured.parent_path().parent_path())!=folded(managedRoot)))return {};
    if(managedRoot.empty()||!managedRoot.is_absolute())return {};
    fs::path newest;fs::file_time_type modified=fs::file_time_type::min();
    for(fs::directory_iterator it(managedRoot,ec),end;!ec&&it!=end;it.increment(ec)){
        const auto candidate=it->path()/L"codex.exe";
        if(!it->is_directory(ec)||it->is_symlink(ec)||!fs::is_regular_file(candidate,ec)||fs::is_symlink(candidate,ec)){ec.clear();continue;}
        const auto stamp=fs::last_write_time(candidate,ec);if(ec){ec.clear();continue;}
        if(newest.empty()||stamp>modified){newest=candidate;modified=stamp;}
    }
    return newest;
}
}
