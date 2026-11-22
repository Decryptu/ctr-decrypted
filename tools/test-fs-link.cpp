// g++ -std=c++17 -Wall -Wextra -I include -o /tmp/test-fs-link tools/test-fs-link.cpp
//
// include/platform/native_fs_link.hpp: a link to a folder (a symlink on POSIX,
// a junction on Windows, which any user can make there) is seen as a link,
// and NativeFs_RemoveAll removes the link inside a tree it deletes without
// touching what the link points to. On the Windows toolchain std::filesystem
// sees neither, and its remove_all deletes through a junction.
#include <platform/native_fs_link.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static void MakeFolderLink(const fs::path &target, const fs::path &link)
{
#ifdef _WIN32
    const std::string command = "cmd /c mklink /J \"" + link.string() + "\" \"" + target.string() + "\" >NUL";
    assert(std::system(command.c_str()) == 0);
#else
    fs::create_directory_symlink(target, link);
#endif
}

int main()
{
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / ("test-fs-link-" + std::to_string((long)std::rand()));
    fs::remove_all(root, ec);
    const fs::path victim = root / "victim", doomed = root / "doomed";
    fs::create_directories(victim);
    fs::create_directories(doomed / "sub");
    std::ofstream(victim / "keep.txt") << "keep";
    std::ofstream(doomed / "sub" / "a.txt") << "a";
    std::ofstream(doomed / "b.txt") << "b";
    MakeFolderLink(victim, doomed / "link");

    assert(NativeFs_IsLink(doomed / "link"));
    assert(!NativeFs_IsLink(doomed) && !NativeFs_IsLink(doomed / "b.txt"));
    assert(NativeFs_IsPlainFile(doomed / "b.txt", ec) && !NativeFs_IsPlainFile(doomed / "link", ec));

    // The tree goes, with the link itself; the folder it points to stays whole.
    assert(NativeFs_RemoveAll(doomed, ec) == 5 && !ec);
    assert(!fs::exists(doomed) && fs::exists(victim / "keep.txt"));

    // A link given directly is removed as a link.
    MakeFolderLink(victim, root / "direct");
    assert(NativeFs_RemoveAll(root / "direct", ec) == 1 && !ec);
    assert(!fs::exists(root / "direct") && fs::exists(victim / "keep.txt"));

    // Nothing there is not an error.
    assert(NativeFs_RemoveAll(root / "missing", ec) == 0 && !ec);

    fs::remove_all(root, ec);
    std::printf("PASS: links are seen and never followed by NativeFs_RemoveAll\n");
    return 0;
}
