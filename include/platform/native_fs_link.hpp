#ifndef NATIVE_FS_LINK_HPP
#define NATIVE_FS_LINK_HPP

// Links and deletion that never leaves the folder it was given.
//
// The MinGW libstdc++ that builds the Windows client does not implement
// symlinks: std::filesystem::is_symlink is false for a junction or a symbolic
// link, symlink_status reports the target, and remove_all walks into a
// junction and deletes the files it points to. Any user can make a junction
// without rights, so every link check and every recursive delete of a folder
// the player can reach goes through these two instead.
//   NativeFs_IsLink: true for a symlink, and on Windows for any reparse point
//     (junction, symbolic link, mount point).
//   NativeFs_IsPlainFile: a regular file that is not a link.
//   NativeFs_RemoveAll: delete a file or a folder tree; a link anywhere in it
//     is removed itself, never followed. Returns the number of entries removed,
//     or -1 with ec set; ec is cleared for a path that does not exist.

#include <cstdint>
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

inline bool NativeFs_IsLink(const std::filesystem::path &path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    return std::filesystem::is_symlink(path, ec);
#endif
}

// A regular file that is not a link (symlink_status alone misses a Windows link).
inline bool NativeFs_IsPlainFile(const std::filesystem::path &path, std::error_code &ec)
{
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(path, ec)) && !NativeFs_IsLink(path);
}

inline std::uintmax_t NativeFs_RemoveTree(const std::filesystem::path &path, std::error_code &ec)
{
    std::uintmax_t removed = 0;
    if (!NativeFs_IsLink(path) && std::filesystem::is_directory(std::filesystem::symlink_status(path, ec)))
    {
        std::filesystem::directory_iterator it(path, ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            removed += NativeFs_RemoveTree(it->path(), ec);
            if (ec) return removed;
        }
        if (ec) return removed;
    }
    ec.clear();
    // A link to a folder is removed as the link: RemoveDirectoryW on Windows,
    // unlink on POSIX. Neither touches the target.
    if (std::filesystem::remove(path, ec)) removed++;
    return removed;
}

inline std::intmax_t NativeFs_RemoveAll(const std::filesystem::path &path, std::error_code &ec)
{
    ec.clear();
    const auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) { ec.clear(); return 0; }
    if (ec) return -1;
    const std::uintmax_t removed = NativeFs_RemoveTree(path, ec);
    return ec ? -1 : (std::intmax_t)removed;
}

#endif
