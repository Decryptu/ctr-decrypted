// Small, release-scoped Project Saphi downloader for manager-light. It lives in
// the isolated C++ AP archive because that target already owns TLS, JSON and a
// real thread runtime; the C unity build only sees the narrow API above.

#include "ap_custom_track_download.h"
#include "ap_seed_content_logic.h" // ap_seed_size_agrees, ap_seed_root_safe (Current Seed, #133 PR 7)
#ifdef CTR_CUSTOM_PACKAGES
// Authoring build only: the Track Manager's Saphi catalogue and package install.
#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_package.h>
#include <platform/native_racer_library.h>
#include <platform/native_track_picture.h>
#include <zlib.h>
#include <chrono>
#include <future>
#endif

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <cerrno>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <direct.h>
#else
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <openssl/ssl.h>
#endif

namespace
{
constexpr size_t kMetadataMax = 256 * 1024;
constexpr size_t kTrackFileMax = 8 * 1024 * 1024;
constexpr const char *kSaphiHost = "www.projectsaphi.com";
constexpr const char *kCatalogueHost = "raw.githubusercontent.com";
constexpr const char *kTrackRoot = "/Decryptu/ctr-decrypted-tracks/main/";

struct DownloadState
{
	std::mutex mutex;
	int state = AP_CT_DOWNLOAD_IDLE;
	std::string detail = "Ready to download from Project Saphi.";
};

// Intentionally process-lifetime. A detached request must not race a static
// destructor if the player closes the game while Windows is still unwinding an
// HTTPS read.
DownloadState &download_state()
{
	static DownloadState *state = new DownloadState();
	return *state;
}

void set_state(int state, const std::string &detail)
{
	DownloadState &shared = download_state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	shared.state = state;
	shared.detail = detail;
}

bool saphi_url_path(const std::string &url, std::string &path)
{
	const std::string prefix = std::string("https://") + kSaphiHost;
	if (url.compare(0, prefix.size(), prefix) != 0)
		return false;
	path = url.substr(prefix.size());
	return !path.empty() && path[0] == '/' && path.find_first_of("\r\n") == std::string::npos;
}

std::string lower_ascii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

// What error messages call a host.
std::string server_name(const char *host)
{
	return std::strcmp(host, kSaphiHost) == 0 ? std::string("Project Saphi") : std::string(host);
}

bool decode_http_response(const char *host, const std::vector<unsigned char> &raw, size_t max_bytes,
	                      std::vector<unsigned char> &body, std::string &error)
{
	const std::string marker = "\r\n\r\n";
	auto header_end = std::search(raw.begin(), raw.end(), marker.begin(), marker.end());
	if (header_end == raw.end())
	{
		error = server_name(host) + " returned an invalid HTTP response.";
		return false;
	}
	const size_t header_bytes = static_cast<size_t>(header_end - raw.begin());
	const std::string headers(reinterpret_cast<const char *>(raw.data()), header_bytes);
	const size_t line_end = headers.find("\r\n");
	if (line_end == std::string::npos || headers.substr(0, line_end).find(" 200 ") == std::string::npos)
	{
		error = server_name(host) + " did not return the requested file.";
		return false;
	}

	const std::string lower = lower_ascii(headers);
	const size_t body_start = header_bytes + marker.size();
	if (body_start > raw.size())
		return false;
	if (lower.find("transfer-encoding: chunked") == std::string::npos)
	{
		if (raw.size() - body_start > max_bytes)
		{
			error = server_name(host) + " returned a file larger than this client accepts.";
			return false;
		}
		body.assign(raw.begin() + body_start, raw.end());
		return true;
	}

	size_t cursor = body_start;
	body.clear();
	while (cursor < raw.size())
	{
		auto size_end = std::search(raw.begin() + cursor, raw.end(), marker.begin(), marker.begin() + 2);
		if (size_end == raw.end())
			break;
		std::string size_text(reinterpret_cast<const char *>(&raw[cursor]),
		                      static_cast<size_t>(size_end - (raw.begin() + cursor)));
		const size_t semicolon = size_text.find(';');
		if (semicolon != std::string::npos)
			size_text.resize(semicolon);
		char *end = nullptr;
		unsigned long chunk = std::strtoul(size_text.c_str(), &end, 16);
		if (end == size_text.c_str() || *end != '\0')
			break;
		cursor = static_cast<size_t>(size_end - raw.begin()) + 2;
		if (chunk == 0)
			return true;
		if (chunk > max_bytes || body.size() + chunk > max_bytes || cursor + chunk + 2 > raw.size())
			break;
		body.insert(body.end(), raw.begin() + cursor, raw.begin() + cursor + chunk);
		cursor += chunk;
		if (raw[cursor] != '\r' || raw[cursor + 1] != '\n')
			break;
		cursor += 2;
	}
	error = server_name(host) + " returned an incomplete download.";
	return false;
}

#ifdef _WIN32
std::wstring widen_ascii(const std::string &text)
{
	return std::wstring(text.begin(), text.end());
}

bool https_get(const char *host, const std::string &path, size_t max_bytes,
	           std::vector<unsigned char> &body, std::string &error)
{
	HINTERNET session = WinHttpOpen(L"CTR-Decrypted/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
	{
		error = "Could not start Windows HTTPS.";
		return false;
	}
	WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
	const std::wstring wide_host = widen_ascii(host);
	HINTERNET connection = WinHttpConnect(session, wide_host.c_str(),
	                                      INTERNET_DEFAULT_HTTPS_PORT, 0);
	const std::wstring wide_path = widen_ascii(path);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", wide_path.c_str(),
	                                                    nullptr, WINHTTP_NO_REFERER,
	                                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                                    WINHTTP_FLAG_SECURE) : nullptr;
	bool ok = request && WinHttpSendRequest(request, L"Accept-Encoding: identity\r\n", -1,
	                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	          WinHttpReceiveResponse(request, nullptr);
	DWORD status = 0;
	DWORD status_size = sizeof status;
	if (ok)
		ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
		                         WINHTTP_NO_HEADER_INDEX) && status == 200;
	while (ok)
	{
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
		{
			ok = false;
			break;
		}
		if (available == 0)
			break;
		if (body.size() + available > max_bytes)
		{
			error = server_name(host) + " returned a file larger than this client accepts.";
			ok = false;
			break;
		}
		const size_t old_size = body.size();
		body.resize(old_size + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, body.data() + old_size, available, &read))
		{
			ok = false;
			break;
		}
		body.resize(old_size + read);
	}
	if (!ok && error.empty())
		error = status == 0 ? "Could not reach " + server_name(host) + "." : server_name(host) + " did not return the requested file.";
	if (request) WinHttpCloseHandle(request);
	if (connection) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return ok;
}
#else
#ifdef CTR_CUSTOM_PACKAGES
// The authoring build carries the CA-trust and timeout fixes from the custom
// content campaign (SteamOS lacks the build host's OPENSSLDIR, and a stalled
// catalogue request must not hold the Track Manager forever). The player build
// keeps the resolver it was tested with, below.
bool load_ca_store(asio::ssl::context &context)
{
	asio::error_code ec;
	const char *env = std::getenv("SSL_CERT_FILE");
	const char *envDir = std::getenv("SSL_CERT_DIR");
	/* Honour explicit trust configuration; do not silently replace an invalid
	   override with system roots. The file API loads roots now, unlike the
	   lazy default-directory lookup that can report success with no usable CA.
	   In particular SteamOS does not have the build host's OPENSSLDIR. */
	if (env && *env)
	{
		context.load_verify_file(env, ec);
		if (ec) return false;
	}
	if (envDir && *envDir)
	{
		context.add_verify_path(envDir, ec);
		if (ec) return false;
	}
	if ((env && *env) || (envDir && *envDir)) return true;
	const char *const candidates[] = {
		X509_get_default_cert_file(),
		"/etc/ssl/cert.pem",
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/ca-bundle.pem"
	};
	for (const char *candidate : candidates)
	{
		if (!candidate || !*candidate)
			continue;
		/* stdio works for SteamOS overlayfs inode numbers that overflow
		   32-bit stat/readdir. Same strategy as the AP transport resolver. */
		FILE *file = std::fopen(candidate, "rb");
		if (!file) continue;
		const bool populated = std::fgetc(file) != EOF;
		std::fclose(file);
		if (!populated) continue;
		ec.clear();
		context.load_verify_file(candidate, ec);
		if (!ec)
		{
			std::fprintf(stderr, "[CustomTracks] HTTPS trust bundle: %s\n", candidate);
			return true;
		}
	}
	/* Directory-only distributions can still use OpenSSL's defaults. Never
	   treat this as proof of connectivity: hostname/peer verification remains
	   mandatory at the actual handshake. */
	context.set_default_verify_paths(ec);
	return !ec;
}

template<typename T> T saphi_await(asio::io_context &io, std::future<T> result,
                                  std::chrono::steady_clock::time_point deadline)
{
    io.restart();
    io.run_until(deadline);
    if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        throw std::runtime_error("the request timed out; check connection and retry");
    return result.get();
}

#else
bool load_ca_store(asio::ssl::context &context)
{
	asio::error_code ec;
	context.set_default_verify_paths(ec);
	if (!ec)
		return true;
	const char *env = std::getenv("SSL_CERT_FILE");
	const char *const candidates[] = {
		env,
		"/etc/ssl/cert.pem",
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/ca-bundle.pem"
	};
	for (const char *candidate : candidates)
	{
		if (!candidate || !*candidate)
			continue;
		ec.clear();
		context.load_verify_file(candidate, ec);
		if (!ec)
			return true;
	}
	return false;
}

#endif

bool https_get(const char *host, const std::string &path, size_t max_bytes,
	           std::vector<unsigned char> &body, std::string &error)
{
	try
	{
		asio::io_context io;
		asio::ssl::context context(asio::ssl::context::tls_client);
		if (!load_ca_store(context))
		{
			error = "No usable TLS certificate store was found.";
			return false;
		}
		asio::ssl::stream<asio::ip::tcp::socket> stream(io, context);
		if (!SSL_set_tlsext_host_name(stream.native_handle(), host))
			throw std::runtime_error("TLS host setup failed");
		stream.set_verify_mode(asio::ssl::verify_peer);
		stream.set_verify_callback(asio::ssl::host_name_verification(host));
		asio::ip::tcp::resolver resolver(io);
#ifdef CTR_CUSTOM_PACKAGES
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
		auto endpoints = saphi_await(io, resolver.async_resolve(host, "443", asio::use_future), deadline);
		saphi_await(io, asio::async_connect(stream.next_layer(), endpoints, asio::use_future), deadline);
		saphi_await(io, stream.async_handshake(asio::ssl::stream_base::client, asio::use_future), deadline);
#else
		asio::connect(stream.next_layer(), resolver.resolve(host, "443"));
		stream.handshake(asio::ssl::stream_base::client);
#endif
		const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
		                            "\r\nUser-Agent: CTR-Decrypted/1.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
#ifdef CTR_CUSTOM_PACKAGES
		saphi_await(io, asio::async_write(stream, asio::buffer(request), asio::use_future), deadline);
#else
		asio::write(stream, asio::buffer(request));
#endif
		std::vector<unsigned char> raw;
		unsigned char chunk[16384];
		asio::error_code ec;
		for (;;)
		{
#ifdef CTR_CUSTOM_PACKAGES
			size_t got = saphi_await(io, stream.async_read_some(asio::buffer(chunk),
			                         asio::redirect_error(asio::use_future, ec)), deadline);
#else
			size_t got = stream.read_some(asio::buffer(chunk), ec);
#endif
			if (got)
			{
				if (raw.size() + got > max_bytes + 64 * 1024)
				{
					error = server_name(host) + " returned a file larger than this client accepts.";
					return false;
				}
				raw.insert(raw.end(), chunk, chunk + got);
			}
			if (ec == asio::error::eof || ec == asio::ssl::error::stream_truncated)
				break;
			if (ec)
				throw asio::system_error(ec);
		}
		return decode_http_response(host, raw, max_bytes, body, error);
	}
	catch (const std::exception &e)
	{
		error = "Could not reach " + server_name(host) + ": " + e.what();
		return false;
	}
}
#endif

bool hash_matches(const std::vector<unsigned char> &bytes, const std::string &expected)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digest_size = 0;
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool ok = context && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
	          EVP_DigestUpdate(context, bytes.data(), bytes.size()) == 1 &&
	          EVP_DigestFinal_ex(context, digest, &digest_size) == 1 && digest_size == 32;
	if (context)
		EVP_MD_CTX_free(context);
	if (!ok || expected.size() != 64)
		return false;
	static const char hex[] = "0123456789abcdef";
	for (unsigned int i = 0; i < digest_size; i++)
		if (hex[digest[i] >> 4] != std::tolower(static_cast<unsigned char>(expected[i * 2])) ||
		    hex[digest[i] & 15] != std::tolower(static_cast<unsigned char>(expected[i * 2 + 1])))
			return false;
	return true;
}

bool write_file(const std::string &path, const std::vector<unsigned char> &bytes)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	file.flush();
	return file.good();
}

bool replace_file(const std::string &source, const std::string &target)
{
#ifdef _WIN32
	return MoveFileExA(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return std::rename(source.c_str(), target.c_str()) == 0;
#endif
}

std::string join_path(const std::string &left, const char *right)
{
	if (left.empty())
		return {};
	return left + ((left.back() == '/' || left.back() == '\\') ? "" : "/") + right;
}

// Current Seed installs (#133 PR 7) create the package folder themselves,
// and only after both files are verified, so a refused download leaves no
// trace. Nothing they write may be reached through a link: the package
// folder, its original folder and every file name are checked without
// following links (lstat; a reparse point on Windows).
enum NodeKind { kNodeAbsent, kNodeDirectory, kNodeFile, kNodeOther };

NodeKind node_kind(const std::string &path)
{
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesA(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? kNodeAbsent : kNodeOther;
	if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		return kNodeOther;
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? kNodeDirectory : kNodeFile;
#else
	struct stat st;
	if (lstat(path.c_str(), &st) != 0)
		return errno == ENOENT || errno == ENOTDIR ? kNodeAbsent : kNodeOther;
	if (S_ISDIR(st.st_mode)) return kNodeDirectory;
	if (S_ISREG(st.st_mode)) return kNodeFile;
	return kNodeOther;
#endif
}

bool make_directory(const std::string &path)
{
	const NodeKind kind = node_kind(path);
	if (kind != kNodeAbsent)
		return kind == kNodeDirectory;
#ifdef _WIN32
	_mkdir(path.c_str());
#else
	mkdir(path.c_str(), 0755);
#endif
	return node_kind(path) == kNodeDirectory;
}

// 1 when path does not exist, 2 when it holds exactly `expected`, 0 otherwise
// (another file, a link, or a name that is not a regular file).
int existing_target(const std::string &path, const std::string &expected);

struct MediaFile
{
	std::string path;
	size_t size = 0;
};

// The package-library parts between here and the Track Manager section are
// spelled `#if defined(...)`: test-seed-content-download-production.py cuts
// the pinned downloader out of this file at the next `#ifdef` of it.
#if defined(CTR_CUSTOM_PACKAGES)
// Install verified LEV and VRM bytes into the package library as the package
// Browse makes for the same Saphi revision (defined with the Browse installer
// below). Returns an empty string, or why nothing was installed.
std::string saphi_install_library(const std::string &assets, const std::string &api_path,
                                  const MediaFile &lev_media, const MediaFile &vrm_media,
                                  const std::string &version, const std::string &title,
                                  const std::string &author, int laps,
                                  const std::vector<unsigned char> &lev, const std::vector<unsigned char> &vrm);
#endif

bool find_media(const nlohmann::json &root, const std::string &type,
	            const std::string &version, MediaFile &media)
{
	if (!root.contains("data") || !root["data"].is_array())
		return false;
	int matches = 0;
	for (const auto &row : root["data"])
	{
		if (!row.is_object() || row.value("type", "") != type || row.value("version", "") != version)
			continue;
		const std::string path = row.value("download_url", "");
		const long long size = row.value("file_size", 0LL);
		if (path.rfind("/api/v2/tracks/101/downloads/", 0) != 0 || size <= 0 || size > static_cast<long long>(kTrackFileMax))
			continue;
		media.path = path;
		media.size = static_cast<size_t>(size);
		matches++;
	}
	return matches == 1;
}

// Publish a verified pinned install (Current Seed, #133 PR 7). It never
// writes through a link, never replaces or removes a file it did not write,
// and never takes anything back: each file is written to an unnamed file and
// then given its name with linkat, which fails when the name exists.
//
// POSIX works on directory handles opened without following links:
// <tracks>/<folder> and its original/ are created and opened with
// O_NOFOLLOW. The data goes into an O_TMPFILE file in original/ and is named
// through /proc/self/fd, so no temporary name exists that anything could
// swap. Where the file system has no O_TMPFILE, a fresh exclusive name is
// used: it is linked and later removed only while it still is this install's
// open file (a swap in the instant between that check and the call remains
// possible on such file systems). A name counts as published only
// when it is this install's still-open file, and the bytes are read back
// through that handle. The tracks folder itself is the player's
// configuration and may be a link.
//
// A failure after the LEV is published leaves that verified LEV in place:
// the track then shows Missing, and installing again keeps it and adds the
// VRM. Windows takes the same steps by path (exclusive create of a fresh
// name, a move that never replaces); a folder swapped for a junction between
// the check and the write is not caught there.
// `lev` / `vrm` are null when that file already is the pinned one.
void publish_pinned(const std::string &tracks, const std::string &folder,
                    const std::vector<unsigned char> *lev, const std::vector<unsigned char> *vrm)
{
	static const char *const names[2] = {"track.lev", "track.vrm"};
	const std::vector<unsigned char> *bytes[2] = {lev, vrm};
	bool done[2] = {false, false};
	std::string failure;
#ifdef _WIN32
	const std::string original = join_path(join_path(tracks, folder.c_str()), "original");
	if (!make_directory(join_path(tracks, folder.c_str())) || !make_directory(original))
		throw std::runtime_error("Could not create the track folder. Nothing was installed.");
	for (int i = 0; failure.empty() && i < 2; ++i)
	{
		if (!bytes[i]) continue;
		std::string temp;
		HANDLE h = INVALID_HANDLE_VALUE;
		for (int k = 0; k < 64 && h == INVALID_HANDLE_VALUE; ++k)
		{
			char name[64];
			std::snprintf(name, sizeof name, ".saphi-%lu-%d.part", static_cast<unsigned long>(GetCurrentProcessId()), k);
			temp = join_path(original, name);
			h = CreateFileA(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
		}
		DWORD wrote = 0;
		const bool ok = h != INVALID_HANDLE_VALUE &&
		                WriteFile(h, bytes[i]->data(), static_cast<DWORD>(bytes[i]->size()), &wrote, nullptr) &&
		                wrote == bytes[i]->size() && FlushFileBuffers(h);
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
		if (ok && MoveFileExA(temp.c_str(), join_path(original, names[i]).c_str(), MOVEFILE_WRITE_THROUGH))
			done[i] = true;
		else
		{
			if (h != INVALID_HANDLE_VALUE) std::remove(temp.c_str());
			failure = "Could not finish installing the verified Saphi files.";
		}
	}
#else
	const int tracks_fd = open(tracks.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (tracks_fd < 0)
		throw std::runtime_error("Could not open the tracks folder. Nothing was installed.");
	int root_fd = -1, original_fd = -1;
	if ((mkdirat(tracks_fd, folder.c_str(), 0755) != 0 && errno != EEXIST) ||
	    (root_fd = openat(tracks_fd, folder.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0 ||
	    (mkdirat(root_fd, "original", 0755) != 0 && errno != EEXIST) ||
	    (original_fd = openat(root_fd, "original", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0)
		failure = "Its folder is a link or could not be created.";
	// Each written file stays open until the end, so its inode cannot be
	// reused: a published name is ours only while it is that open file.
	int fds[2] = {-1, -1};
	struct stat mine[2];
	for (int i = 0; failure.empty() && i < 2; ++i)
	{
		if (!bytes[i]) continue;
		char temp[64] = "";
		int fd = -1;
#ifdef O_TMPFILE
		fd = openat(original_fd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0644);
#endif
		for (int k = 0; fd < 0 && k < 64; ++k)
		{
			std::snprintf(temp, sizeof temp, ".saphi-%d-%d.part", static_cast<int>(getpid()), k);
			fd = openat(original_fd, temp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
			if (fd < 0 && errno != EEXIST) break;
		}
		fds[i] = fd;
		// Identify the file before writing, so cleanup after a failed write or
		// fsync still knows which fallback name is this install's own.
		const bool identified = fd >= 0 && fstat(fd, &mine[i]) == 0;
		size_t at = 0;
		while (identified && at < bytes[i]->size())
		{
			const ssize_t n = write(fd, bytes[i]->data() + at, bytes[i]->size() - at);
			if (n <= 0) break;
			at += static_cast<size_t>(n);
		}
		const bool written = identified && at == bytes[i]->size() && fsync(fd) == 0;
		bool linked = false;
		// The fallback's fresh name is used, and later removed, only while it
		// still is this install's open file.
		auto temp_is_mine = [&]() {
			struct stat now;
			return fstatat(original_fd, temp, &now, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(now.st_mode) &&
			       now.st_dev == mine[i].st_dev && now.st_ino == mine[i].st_ino;
		};
		bool temp_swapped = false;
		if (written && temp[0])
		{
			temp_swapped = !temp_is_mine();
			linked = !temp_swapped && linkat(original_fd, temp, original_fd, names[i], 0) == 0;
		}
		else if (written)
		{
			char self[48];
			std::snprintf(self, sizeof self, "/proc/self/fd/%d", fd);
			linked = linkat(AT_FDCWD, self, original_fd, names[i], AT_SYMLINK_FOLLOW) == 0;
		}
		const bool exists_already = written && !linked && !temp_swapped && errno == EEXIST;
		if (fd >= 0 && temp[0] && (!identified || temp_is_mine())) unlinkat(original_fd, temp, 0); // the fallback's own fresh name (unidentified only when fstat failed right after its exclusive create)
		struct stat published;
		if (linked && fstatat(original_fd, names[i], &published, AT_SYMLINK_NOFOLLOW) == 0 &&
		    S_ISREG(published.st_mode) && published.st_dev == mine[i].st_dev && published.st_ino == mine[i].st_ino)
			done[i] = true;
		else if (linked || temp_swapped)
			failure = "Its folder changed during the install. Check tracks/" + folder + "/original.";
		else
			failure = exists_already ? "A track file appeared in its folder during the install."
			                         : "Could not finish installing the verified Saphi files.";
	}
	// Read each published file back through its own handle: bytes changed
	// since verification fail the install.
	for (int i = 0; failure.empty() && i < 2; ++i)
	{
		if (!done[i]) continue;
		std::vector<unsigned char> back(bytes[i]->size());
		size_t at = 0;
		while (at < back.size())
		{
			const ssize_t n = pread(fds[i], back.data() + at, back.size() - at, static_cast<off_t>(at));
			if (n <= 0) break;
			at += static_cast<size_t>(n);
		}
		if (at != back.size() || back != *bytes[i])
			failure = "A track file changed while it was installed. Rescan, then try again.";
	}
	for (int fd : fds)
		if (fd >= 0) close(fd);
	if (original_fd >= 0) close(original_fd);
	if (root_fd >= 0) close(root_fd);
	close(tracks_fd);
#endif
	if (!failure.empty())
		throw std::runtime_error(failure + (done[0] && !done[1] && bytes[1]
		                                    ? " The verified LEV is in place; Install again adds the VRM."
		                                    : done[0] || done[1] ? "" : " Nothing was installed."));
}

// `lev_bytes` / `vrm_bytes` are the room's size pins and `tracks` the folder
// the package root must sit in (Current Seed, #133 PR 7); all zero / empty for
// the legacy single-package download, which keeps its old behaviour.
// One install. `tracks` set: a pinned Current Seed install; with `library`
// set too (the assets folder) it goes into the package library instead of
// <tracks>/<folder>, under `title`, `author` and `laps`.
struct LibraryTarget
{
	std::string assets, title, author;
	int laps = 0;
};

void worker(std::string package_root, std::string api_url, std::string version,
	        std::string lev_hash, std::string vrm_hash,
	        uint32_t lev_pin, uint32_t vrm_pin, std::string tracks, LibraryTarget library)
{
	const bool pinned = !tracks.empty();
	try
	{
		std::string api_path;
		if (!saphi_url_path(api_url, api_path))
			throw std::runtime_error("The release registry has an invalid Saphi API URL.");
		if (pinned && library.assets.empty() && !ap_seed_root_safe(tracks.c_str(), package_root.c_str()))
			throw std::runtime_error("The track folder is not a safe place to install. Nothing was changed.");
		set_state(AP_CT_DOWNLOAD_RUNNING, "Contacting Project Saphi...");
		std::vector<unsigned char> metadata;
		std::string error;
		if (!https_get(kSaphiHost, api_path, kMetadataMax, metadata, error))
			throw std::runtime_error(error);
		nlohmann::json root = nlohmann::json::parse(metadata.begin(), metadata.end());
		MediaFile lev;
		MediaFile vrm;
		if (!find_media(root, "lev", version, lev) || !find_media(root, "vrm", version, vrm))
			throw std::runtime_error("Saphi does not list exactly one matching LEV and VRM for this release.");
		if (!ap_seed_size_agrees(lev_pin, lev.size) || !ap_seed_size_agrees(vrm_pin, vrm.size))
			throw std::runtime_error("Saphi lists another file size than this seed pins. Nothing was changed.");

		std::vector<unsigned char> lev_bytes;
		std::vector<unsigned char> vrm_bytes;
		set_state(AP_CT_DOWNLOAD_RUNNING, "Downloading the track (LEV) from Saphi...");
		if (!https_get(kSaphiHost, lev.path, kTrackFileMax, lev_bytes, error) || lev_bytes.size() != lev.size)
			throw std::runtime_error(error.empty() ? "The Saphi LEV download was incomplete." : error);
		if (!hash_matches(lev_bytes, lev_hash))
			throw std::runtime_error("The downloaded LEV does not match this release registry.");

		set_state(AP_CT_DOWNLOAD_RUNNING, "Downloading the track (VRM) from Saphi...");
		if (!https_get(kSaphiHost, vrm.path, kTrackFileMax, vrm_bytes, error) || vrm_bytes.size() != vrm.size)
			throw std::runtime_error(error.empty() ? "The Saphi VRM download was incomplete." : error);
		if (!hash_matches(vrm_bytes, vrm_hash))
			throw std::runtime_error("The downloaded VRM does not match this release registry.");
		if (!ap_seed_size_agrees(lev_pin, lev_bytes.size()) || !ap_seed_size_agrees(vrm_pin, vrm_bytes.size()))
			throw std::runtime_error("The downloaded files are not the size this seed pins. Nothing was changed.");

#if defined(CTR_CUSTOM_PACKAGES)
		if (pinned && !library.assets.empty())
		{
			set_state(AP_CT_DOWNLOAD_RUNNING, "Download verified. Adding it to the library...");
			const std::string failure = saphi_install_library(library.assets, api_path, lev, vrm, version, library.title,
			                                                  library.author, library.laps, lev_bytes, vrm_bytes);
			if (!failure.empty()) throw std::runtime_error(failure);
			set_state(AP_CT_DOWNLOAD_SUCCEEDED, "Download verified. Installed in the library.");
			return;
		}
#endif
		const std::string original = join_path(package_root, "original");
		const std::string lev_temp = join_path(original, ".saphi-track.lev.part");
		const std::string vrm_temp = join_path(original, ".saphi-track.vrm.part");
		const std::string lev_target = join_path(original, "track.lev");
		const std::string vrm_target = join_path(original, "track.vrm");
		bool write_lev = true, write_vrm = true;
		if (pinned)
		{
			const NodeKind root_kind = node_kind(package_root), original_kind = node_kind(original);
			if ((root_kind != kNodeAbsent && root_kind != kNodeDirectory) ||
			    (original_kind != kNodeAbsent && original_kind != kNodeDirectory) ||
			    (root_kind == kNodeAbsent && original_kind != kNodeAbsent))
				throw std::runtime_error("Its folder is a link or not a folder. Move it out, then try again. "
				                         "Nothing was changed.");
			// Never replace a player's file: an existing track.lev / track.vrm
			// must already be the pinned bytes, and is then kept as it is.
			const int lev_existing = existing_target(lev_target, lev_hash);
			const int vrm_existing = existing_target(vrm_target, vrm_hash);
			if (!lev_existing || !vrm_existing)
				throw std::runtime_error("Its folder already has a different track.lev or track.vrm. "
				                         "Move it out, then try again. Nothing was changed.");
			write_lev = lev_existing == 1;
			write_vrm = vrm_existing == 1;
			publish_pinned(tracks, package_root.substr(tracks.size() + 1), write_lev ? &lev_bytes : nullptr,
			               write_vrm ? &vrm_bytes : nullptr);
			set_state(AP_CT_DOWNLOAD_SUCCEEDED, "Download verified. Finishing local setup...");
			return;
		}
		if ((write_lev && !write_file(lev_temp, lev_bytes)) || (write_vrm && !write_file(vrm_temp, vrm_bytes)))
		{
			std::remove(lev_temp.c_str());
			std::remove(vrm_temp.c_str());
			throw std::runtime_error("Could not write the downloaded files to the track folder.");
		}
		if ((write_lev && !replace_file(lev_temp, lev_target)) || (write_vrm && !replace_file(vrm_temp, vrm_target)))
		{
			std::remove(lev_temp.c_str());
			std::remove(vrm_temp.c_str());
			throw std::runtime_error("Could not finish installing the verified Saphi files.");
		}
		set_state(AP_CT_DOWNLOAD_SUCCEEDED, "Download verified. Finishing local setup...");
	}
	catch (const std::exception &e)
	{
		set_state(AP_CT_DOWNLOAD_FAILED, e.what());
	}
}

int existing_target(const std::string &path, const std::string &expected)
{
	struct stat st;
	const NodeKind kind = node_kind(path);
	if (kind == kNodeAbsent)
		return 1;
	if (kind != kNodeFile || stat(path.c_str(), &st) != 0 || st.st_size <= 0 ||
	    static_cast<uint64_t>(st.st_size) > kTrackFileMax)
		return 0;
	std::ifstream file(path, std::ios::binary);
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return file.good() || file.eof() ? (hash_matches(bytes, expected) ? 2 : 0) : 0;
}

int start_worker(const char *package_root, const char *download_api_url, const char *package_version,
	             const char *lev_sha256, const char *vrm_sha256,
	             uint32_t lev_bytes, uint32_t vrm_bytes, const char *tracks,
	             LibraryTarget library = LibraryTarget())
{
	DownloadState &shared = download_state();
	{
		std::lock_guard<std::mutex> lock(shared.mutex);
		if (shared.state == AP_CT_DOWNLOAD_RUNNING)
			return 0;
		shared.state = AP_CT_DOWNLOAD_RUNNING;
		shared.detail = "Starting Project Saphi download...";
	}
	try
	{
		std::thread(worker, std::string(package_root), std::string(download_api_url), std::string(package_version),
		            std::string(lev_sha256), std::string(vrm_sha256), lev_bytes, vrm_bytes,
		            std::string(tracks ? tracks : ""), library).detach();
		return 1;
	}
	catch (const std::exception &e)
	{
		set_state(AP_CT_DOWNLOAD_FAILED, std::string("Could not start downloader: ") + e.what());
		return 0;
	}
}
} // namespace

extern "C" int ap_custom_track_download_start(const char *package_root,
	                                           const char *download_api_url,
	                                           const char *package_version,
	                                           const char *lev_sha256,
	                                           const char *vrm_sha256)
{
	if (!package_root || !*package_root || !download_api_url || !*download_api_url ||
	    !package_version || !*package_version || !lev_sha256 || !vrm_sha256)
		return 0;
	return start_worker(package_root, download_api_url, package_version, lev_sha256, vrm_sha256, 0, 0, nullptr);
}

extern "C" int ap_custom_track_download_start_pinned(const char *tracks_dir,
	                                                  const char *package_root,
	                                                  const char *download_api_url,
	                                                  const char *package_version,
	                                                  const char *lev_sha256, uint32_t lev_bytes,
	                                                  const char *vrm_sha256, uint32_t vrm_bytes)
{
	if (!tracks_dir || !*tracks_dir || !package_root || !*package_root || !download_api_url ||
	    !*download_api_url || !package_version || !*package_version || !lev_sha256 || !vrm_sha256 ||
	    !lev_bytes || !vrm_bytes || !ap_seed_root_safe(tracks_dir, package_root))
		return 0;
	return start_worker(package_root, download_api_url, package_version, lev_sha256, vrm_sha256,
	                    lev_bytes, vrm_bytes, tracks_dir);
}

#if defined(CTR_CUSTOM_PACKAGES)
extern "C" int ap_custom_track_download_start_library(const char *assets,
	                                                   const char *package_root,
	                                                   const char *download_api_url,
	                                                   const char *package_version,
	                                                   const char *lev_sha256, uint32_t lev_bytes,
	                                                   const char *vrm_sha256, uint32_t vrm_bytes,
	                                                   const char *title, const char *author, int laps)
{
	LibraryTarget library;
	if (!assets || !*assets || !title || !*title || !package_root || !*package_root || !download_api_url ||
	    !*download_api_url || !package_version || !*package_version || !lev_sha256 || !vrm_sha256 ||
	    !lev_bytes || !vrm_bytes)
		return 0;
	library.assets = assets;
	library.title = title;
	library.author = author ? author : "";
	library.laps = laps;
	return start_worker(package_root, download_api_url, package_version, lev_sha256, vrm_sha256,
	                    lev_bytes, vrm_bytes, package_root, library);
}
#endif

extern "C" int ap_custom_track_download_status(char *detail, int detail_bytes)
{
	DownloadState &shared = download_state();
	std::lock_guard<std::mutex> lock(shared.mutex);
	if (detail && detail_bytes > 0)
		std::snprintf(detail, static_cast<size_t>(detail_bytes), "%s", shared.detail.c_str());
	return shared.state;
}

#ifdef CTR_CUSTOM_PACKAGES
// Track Manager: the Saphi catalogue refresh and the explicit install of one
// catalogue revision into the package store. Authoring build only.
namespace {
// A file of the Decrypted track catalogue, or of the local copy of one that
// CTR_DEV_TRACK_CATALOGUE names.
bool track_catalogue_get(const std::string &path, size_t max, std::vector<unsigned char> &out, std::string &error)
{
    const char *dir = std::getenv("CTR_DEV_TRACK_CATALOGUE");
    if (!dir || !*dir) return https_get(kCatalogueHost, kTrackRoot + path, max, out, error);
    std::ifstream file(join_path(dir, path.c_str()), std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(file), {});
    if (!out.empty() && out.size() <= max) return true;
    error = "Cannot read " + path;
    return false;
}

struct SaphiRefreshState
{
    std::mutex mutex;
    int state = -1;
    CustomSaphiCatalogue *catalogue = nullptr;
    std::string error;
};
SaphiRefreshState &saphi_refresh_state()
{
    /* A game exit cannot race destruction of a detached HTTPS worker. */
    static auto *state = new SaphiRefreshState();
    return *state;
}
void saphi_refresh_worker()
{
    CustomSaphiCatalogue *catalogue = nullptr;
    std::string error;
    nlohmann::json combined = {{"data", nlohmann::json::array()}};
    const char *localCatalogue = std::getenv("CTR_DEV_TRACK_CATALOGUE");
    try
    {
        /* A local catalogue stands in for Saphi too: its saphi.json, or nothing. */
        int pages = localCatalogue && *localCatalogue ? 0 : 1;
        std::vector<unsigned char> localSaphi;
        if (!pages && track_catalogue_get("saphi.json", 4 * 1024 * 1024, localSaphi, error))
            combined = nlohmann::json::parse(localSaphi.begin(), localSaphi.end());
        error.clear();
        size_t totalBytes = 0;
        for (int page = 1; page <= pages; page++)
        {
            const std::string path = "/api/v3/tracks?per_page=100&include_standards=0&include_events=0&include_downloads=1&page=" + std::to_string(page);
            std::vector<unsigned char> body;
            if (!https_get(kSaphiHost, path, 4 * 1024 * 1024, body, error)) throw std::runtime_error(error);
            totalBytes += body.size();
            if (totalBytes > 4 * 1024 * 1024) throw std::runtime_error("Saphi catalogue exceeds this client's capacity");
            auto response = nlohmann::json::parse(body.begin(), body.end());
            const auto &meta = response.at("meta");
            if (!meta.at("current_page").is_number_integer() || meta["current_page"] != page ||
                !meta.at("total_pages").is_number_integer()) throw std::runtime_error("Invalid Saphi pagination");
            int announced = meta["total_pages"].get<int>();
            if (announced < 1 || announced > 11 || (page > 1 && announced != pages))
                throw std::runtime_error("Saphi catalogue changed during refresh; retry");
            pages = announced;
            const auto &rows = response.at("data");
            if (!rows.is_array() || rows.size() > 100 || combined["data"].size() + rows.size() > 1024)
                throw std::runtime_error("Invalid Saphi page size");
            for (const auto &row : rows) combined["data"].push_back(row);
        }
        auto bytes = combined.dump();
        char detail[512];
        if (!CustomSaphi_ParseCatalogue(bytes.data(), bytes.size(), &catalogue, detail, sizeof detail))
            throw std::runtime_error(detail);
    }
    catch (const std::exception &e) { error = e.what(); }
    /* Either source alone is a usable list: one being down hides only its tracks. */
    std::vector<unsigned char> index;
    std::string indexError;
    if (track_catalogue_get("index.json", 1024 * 1024, index, indexError))
    {
        char detail[512];
        const char empty[] = "{\"data\":[]}";
        if (!catalogue && !CustomSaphi_ParseCatalogue(empty, sizeof empty - 1, &catalogue, detail, sizeof detail))
            indexError = detail;
        else if (!CustomSaphi_AddDecrypted(catalogue, reinterpret_cast<const char *>(index.data()), index.size(), detail, sizeof detail))
            indexError = detail;
        else indexError.clear();
    }
    if (!indexError.empty()) std::fprintf(stderr, "[Tracks] catalogue: %s\n", indexError.c_str());
    if (catalogue && CustomSaphi_Count(catalogue) == 0 && !error.empty()) CustomSaphi_Free(&catalogue);
    else if (catalogue && !error.empty()) std::fprintf(stderr, "[Saphi] catalogue: %s\n", error.c_str());
    auto &state = saphi_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.catalogue = catalogue;
    state.error = error;
    state.state = catalogue ? 1 : -2;
}
}
extern "C" int CustomSaphi_StartRefresh(void)
{
    auto &state = saphi_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0;
    state.error.clear();
    try { std::thread(saphi_refresh_worker).detach(); return 1; }
    catch (const std::exception &e) { state.state = -1; state.error = e.what(); return 0; }
}
extern "C" int CustomSaphi_PollRefresh(CustomSaphiCatalogue **out, char *error, size_t errorSize)
{
    auto &state = saphi_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state == 0 || state.state == -1) return state.state;
    if (!out || *out) return 0;
    int result = state.state;
    *out = state.catalogue;
    state.catalogue = nullptr;
    if (error && errorSize) std::snprintf(error, errorSize, "%s", state.error.c_str());
    state.state = -1;
    return result;
}

namespace {
std::string saphi_digest(const void *bytes, size_t size, const EVP_MD *type)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int count = 0;
    if (!EVP_Digest(bytes, size, digest, &count, type, nullptr)) throw std::runtime_error("Cannot hash Saphi payload");
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned int i = 0; i < count; i++) { result += hex[digest[i] >> 4]; result += hex[digest[i] & 15]; }
    return result;
}
std::string saphi_import_uuid(int trackID)
{
    /* Same importer namespace/UUIDv5 recipe as export_custom_content_library. */
    static const unsigned char ns[] = {0x6b,0xa7,0xb8,0x11,0x9d,0xad,0x11,0xd1,0x80,0xb4,0x00,0xc0,0x4f,0xd4,0x30,0xc8};
    std::string input(reinterpret_cast<const char *>(ns), sizeof ns);
    input += "https://www.projectsaphi.com/tracks/" + std::to_string(trackID);
    auto hex = saphi_digest(input.data(), input.size(), EVP_sha1()).substr(0, 32);
    hex[12] = '5';
    int nibble = hex[16] <= '9' ? hex[16] - '0' : hex[16] - 'a' + 10;
    hex[16] = "89ab"[nibble & 3];
    return hex.substr(0,8) + "-" + hex.substr(8,4) + "-" + hex.substr(12,4) + "-" + hex.substr(16,4) + "-" + hex.substr(20);
}
struct SaphiInstallState
{
    std::mutex mutex;
    int state = -1;
    std::string pin, error;
};
SaphiInstallState &saphi_install_state() { static auto *s = new SaphiInstallState(); return *s; }
/* A Decrypted catalogue package: the index pins manifest.json, which pins its
   files, and the package is published as those exact bytes. */
void decrypted_install(const CustomSaphiRevision &revision, const std::string &assets, CustomPackageOwned **owned)
{
    const std::string folder = std::string("tracks/") + revision.folder + "/";
    std::vector<unsigned char> manifest;
    std::string error;
    if (!track_catalogue_get(folder + "manifest.json", 65536, manifest, error)) throw std::runtime_error(error);
    if (manifest.size() != revision.manifestBytes || !hash_matches(manifest, revision.pin))
        throw std::runtime_error("manifest.json does not match the track list; refresh and retry");
    const auto listed = nlohmann::json::parse(manifest.begin(), manifest.end());
    std::vector<std::vector<unsigned char>> bytes;
    std::vector<std::string> roles;
    bytes.reserve(listed.at("files").size());
    roles.reserve(listed.at("files").size());
    std::vector<CustomPackageInput> inputs;
    for (const auto &file : listed.at("files"))
    {
        const auto path = file.at("path").get<std::string>();
        if (path.empty() || path[0] == '/' || path.find("..") != std::string::npos || path.find('\\') != std::string::npos)
            throw std::runtime_error("manifest.json lists an unsafe path");
        bytes.emplace_back();
        if (!track_catalogue_get(folder + path, kTrackFileMax, bytes.back(), error)) throw std::runtime_error(error);
        roles.push_back(file.at("role").get<std::string>());
        inputs.push_back({roles.back().c_str(), bytes.back().data(), bytes.back().size()});
    }
    char detail[512];
    if (!CustomPackage_AcquireBuffers(reinterpret_cast<const char *>(manifest.data()), manifest.size(), revision.pin,
                                      inputs.data(), inputs.size(), owned, detail, sizeof detail) ||
        !CustomPackage_InstallAssets(*owned, assets.c_str(), detail, sizeof detail))
        throw std::runtime_error(detail);
}
void saphi_install(const CustomSaphiRevision &revision, const std::string &assets, CustomPackageOwned **out, std::string &pin)
{
    std::string error;
    std::vector<unsigned char> lev, vrm;
    auto fetch = [&](const CustomSaphiMedia &media, std::vector<unsigned char> &bytes) {
        const std::string expected = "/api/v3/tracks/" + std::to_string(revision.trackID) + "/downloads/" + std::to_string(media.id);
        if (!media.bytes || media.bytes > kTrackFileMax || expected != media.path)
            throw std::runtime_error("Selected Saphi revision has no valid download");
        if (!https_get(kSaphiHost, expected, kTrackFileMax, bytes, error)) throw std::runtime_error(error);
        if (bytes.size() != media.bytes || crc32(0, bytes.data(), static_cast<uInt>(bytes.size())) != media.crc32)
            throw std::runtime_error("Saphi file differs from the selected source revision; refresh and retry");
    };
    fetch(revision.lev, lev); fetch(revision.vrm, vrm);
    /* The track's own music, when Saphi lists one current .sca. It is
       pinned like LEV and VRM; the race decides whether it can play it. */
    std::vector<unsigned char> sca;
    if (revision.sca.id) fetch(revision.sca, sca);
    auto levHash = saphi_digest(lev.data(), lev.size(), EVP_sha256());
    auto vrmHash = saphi_digest(vrm.data(), vrm.size(), EVP_sha256());
    nlohmann::json files = nlohmann::json::array();
    files.push_back({{"role","lev"},{"path","original/lev-" + std::to_string(revision.lev.id) + ".lev"},{"sha256",levHash},{"bytes",lev.size()}});
    nlohmann::json source = {{"schema_version",1},{"provider","projectsaphi"},
        {"track_id",revision.trackID},{"lev_media_id",revision.lev.id},{"vrm_media_id",revision.vrm.id},
        {"version",revision.version},{"author",revision.author}};
    if (!sca.empty()) source["sca_media_id"] = revision.sca.id;
    std::string presentation = source.dump();
    files.push_back({{"role","presentation"},{"path","metadata/saphi-source.json"},
        {"sha256",saphi_digest(presentation.data(),presentation.size(),EVP_sha256())},{"bytes",presentation.size()}});
    std::string settings;
    /* Track-level laps are applied only to current source media. An old
       revision is never silently given today's potentially changed rules. */
    if (revision.current && revision.sourceLaps >= 1 && revision.sourceLaps <= 127)
    {
        settings = nlohmann::json({{"schema_version",1},{"laps",revision.sourceLaps},{"lev_sha256",levHash},{"vrm_sha256",vrmHash}}).dump();
        files.push_back({{"role","race_settings"},{"path","metadata/race-settings.json"},
            {"sha256",saphi_digest(settings.data(),settings.size(),EVP_sha256())},{"bytes",settings.size()}});
    }
    /* Files stay in role order (the manifest pin is over the sorted list). */
    if (!sca.empty())
        files.push_back({{"role","sca"},{"path","original/sca-" + std::to_string(revision.sca.id) + ".sca"},
            {"sha256",saphi_digest(sca.data(),sca.size(),EVP_sha256())},{"bytes",sca.size()}});
    files.push_back({{"role","vrm"},{"path","original/vrm-" + std::to_string(revision.vrm.id) + ".vrm"},{"sha256",vrmHash},{"bytes",vrm.size()}});
    std::string manifest = nlohmann::json({{"schema_version",1},{"package_uuid",saphi_import_uuid(revision.trackID)},
        {"version",revision.version},{"title",revision.title},
        {"compatibility",{{"native_contract",1},{"apworld_contract",1}}},{"files",files}}).dump();
    pin = saphi_digest(manifest.data(),manifest.size(),EVP_sha256());
    std::vector<CustomPackageInput> inputs = {{"lev",lev.data(),lev.size()},{"vrm",vrm.data(),vrm.size()}};
    inputs.push_back({"presentation",presentation.data(),presentation.size()});
    if (!settings.empty()) inputs.push_back({"race_settings",settings.data(),settings.size()});
    if (!sca.empty()) inputs.push_back({"sca",sca.data(),sca.size()});
    char detail[512];
    if (!CustomPackage_AcquireBuffers(manifest.data(),manifest.size(),pin.c_str(),inputs.data(),inputs.size(),out,detail,sizeof detail))
        throw std::runtime_error(detail);
    if (!CustomPackage_InstallAssets(*out, assets.c_str(), detail, sizeof detail)) throw std::runtime_error(detail);
}
void saphi_install_worker(CustomSaphiRevision revision, std::string assets)
{
    CustomPackageOwned *owned = nullptr;
    std::string error, pin;
    try
    {
        if (revision.decrypted)
        {
            decrypted_install(revision, assets, &owned);
            pin = revision.pin;
        }
        else saphi_install(revision, assets, &owned, pin);
    }
    catch (const std::exception &e) { error = e.what(); }
    CustomPackage_Free(&owned);
    auto &state = saphi_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.pin = error.empty() ? pin : "";
    state.error = error;
    state.state = error.empty() ? 1 : -2;
}
}
namespace {
// The id after the last '/' of a Saphi path, or 0.
int saphi_path_id(const std::string &path, size_t from)
{
    int id = 0;
    for (size_t i = from; i < path.size() && path[i] != '/'; i++)
    {
        if (path[i] < '0' || path[i] > '9' || id > 100000000) return 0;
        id = id * 10 + (path[i] - '0');
    }
    return id;
}

std::string saphi_install_library(const std::string &assets, const std::string &api_path,
                                  const MediaFile &lev_media, const MediaFile &vrm_media,
                                  const std::string &version, const std::string &title,
                                  const std::string &author, int laps,
                                  const std::vector<unsigned char> &lev, const std::vector<unsigned char> &vrm)
{
    /* The same package Browse installs for this revision: same UUID, so the
       Installed tab shows the track once, and the same provenance, so it
       names the author and Browse knows it is installed. */
    const size_t tracksAt = api_path.find("/tracks/");
    const int track = tracksAt == std::string::npos ? 0 : saphi_path_id(api_path, tracksAt + 8);
    const int levID = saphi_path_id(lev_media.path, lev_media.path.rfind('/') + 1);
    const int vrmID = saphi_path_id(vrm_media.path, vrm_media.path.rfind('/') + 1);
    if (track <= 0 || levID <= 0 || vrmID <= 0) return "Saphi listed the track in an unexpected form. Nothing was installed.";
    const auto levHash = saphi_digest(lev.data(), lev.size(), EVP_sha256());
    const auto vrmHash = saphi_digest(vrm.data(), vrm.size(), EVP_sha256());
    nlohmann::json files = nlohmann::json::array();
    files.push_back({{"role","lev"},{"path","original/lev-" + std::to_string(levID) + ".lev"},{"sha256",levHash},{"bytes",lev.size()}});
    const std::string presentation = nlohmann::json({{"schema_version",1},{"provider","projectsaphi"},
        {"track_id",track},{"lev_media_id",levID},{"vrm_media_id",vrmID},{"version",version},{"author",author}}).dump();
    files.push_back({{"role","presentation"},{"path","metadata/saphi-source.json"},
        {"sha256",saphi_digest(presentation.data(),presentation.size(),EVP_sha256())},{"bytes",presentation.size()}});
    std::string settings;
    if (laps >= 1 && laps <= 127)
    {
        settings = nlohmann::json({{"schema_version",1},{"laps",laps},{"lev_sha256",levHash},{"vrm_sha256",vrmHash}}).dump();
        files.push_back({{"role","race_settings"},{"path","metadata/race-settings.json"},
            {"sha256",saphi_digest(settings.data(),settings.size(),EVP_sha256())},{"bytes",settings.size()}});
    }
    files.push_back({{"role","vrm"},{"path","original/vrm-" + std::to_string(vrmID) + ".vrm"},{"sha256",vrmHash},{"bytes",vrm.size()}});
    const std::string manifest = nlohmann::json({{"schema_version",1},{"package_uuid",saphi_import_uuid(track)},
        {"version",version},{"title",title},
        {"compatibility",{{"native_contract",1},{"apworld_contract",1}}},{"files",files}}).dump();
    const std::string pin = saphi_digest(manifest.data(),manifest.size(),EVP_sha256());
    std::vector<CustomPackageInput> inputs = {{"lev",lev.data(),lev.size()},{"vrm",vrm.data(),vrm.size()},
        {"presentation",presentation.data(),presentation.size()}};
    if (!settings.empty()) inputs.push_back({"race_settings",settings.data(),settings.size()});
    CustomPackageOwned *owned = nullptr;
    char detail[512];
    std::string failure;
    if (!CustomPackage_AcquireBuffers(manifest.data(),manifest.size(),pin.c_str(),inputs.data(),inputs.size(),&owned,detail,sizeof detail) ||
        !CustomPackage_InstallAssets(owned, assets.c_str(), detail, sizeof detail))
        failure = detail;
    CustomPackage_Free(&owned);
    return failure;
}
}

extern "C" int CustomSaphi_StartInstall(const CustomSaphiRevision *revision, const char *assets)
{
    if (!revision || !assets || !*assets || std::strlen(assets) > 31000 || revision->disabledReason[0] ||
        !std::memchr(revision->title,0,sizeof revision->title) || !std::memchr(revision->version,0,sizeof revision->version) ||
        !std::memchr(revision->lev.path,0,sizeof revision->lev.path) || !std::memchr(revision->vrm.path,0,sizeof revision->vrm.path) ||
        !std::memchr(revision->sca.path,0,sizeof revision->sca.path)) return 0;
    auto &state = saphi_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0; state.pin.clear(); state.error.clear();
    try { std::thread(saphi_install_worker, *revision, std::string(assets)).detach(); return 1; }
    catch (const std::exception &e) { state.state = -1; state.error = e.what(); return 0; }
}
extern "C" int CustomSaphi_PollInstall(char pin[65], char *error, size_t errorSize)
{
    auto &state = saphi_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state == 0 || state.state == -1) return state.state;
    int result = state.state;
    if (pin) std::snprintf(pin,65,"%s",state.pin.c_str());
    if (error && errorSize) std::snprintf(error,errorSize,"%s",state.error.c_str());
    state.state = -1;
    return result;
}
/* The racer catalogue (docs/CUSTOM_RACERS.md): index.json, then per racer its
   racer.json (checked against the index) and the files it lists (checked
   against racer.json), from the catalogue repository's main branch. */
namespace {
constexpr const char *kRacerHost = kCatalogueHost;
constexpr const char *kRacerRoot = "/Decryptu/ctr-decrypted-racers/main/";
constexpr int kRacerIndexMax = 256;

struct RacerJobState
{
    std::mutex mutex;
    int state = -1;
    std::vector<NativeRacerCatalogueEntry> entries;
    std::string error;
};
RacerJobState &racer_refresh_state() { static auto *state = new RacerJobState(); return *state; }
RacerJobState &racer_install_state() { static auto *state = new RacerJobState(); return *state; }

void racer_finish(RacerJobState &state, int result, const std::string &error,
                  std::vector<NativeRacerCatalogueEntry> entries = {})
{
    std::lock_guard<std::mutex> guard(state.mutex);
    state.entries = std::move(entries);
    state.error = error;
    state.state = result;
}

int racer_poll(RacerJobState &state, char *error, size_t errorSize)
{
    if (state.state == 0 || state.state == -1) return state.state;
    int result = state.state;
    if (error && errorSize) std::snprintf(error, errorSize, "%s", state.error.c_str());
    state.state = -1;
    return result;
}

void racer_refresh_worker()
{
    std::vector<unsigned char> body;
    std::string error;
    if (!https_get(kRacerHost, std::string(kRacerRoot) + "index.json", 1024 * 1024, body, error))
        return racer_finish(racer_refresh_state(), -2, error);
    std::vector<NativeRacerCatalogueEntry> entries(kRacerIndexMax);
    char detail[256] = {0};
    const int count = NativeRacerLibrary_ParseIndex(reinterpret_cast<const char *>(body.data()), body.size(),
                                                    entries.data(), kRacerIndexMax, detail, sizeof detail);
    if (count < 0) return racer_finish(racer_refresh_state(), -2, std::string("The racer list is unreadable: ") + detail);
    entries.resize(count);
    racer_finish(racer_refresh_state(), 1, "", std::move(entries));
}

void racer_install_worker(NativeRacerCatalogueEntry entry, std::string racersDir)
{
    auto &state = racer_install_state();
    const std::string folder = std::string(kRacerRoot) + "racers/" + entry.folder + "/";
    std::vector<unsigned char> manifest, model, portrait;
    std::string error;
    if (!https_get(kRacerHost, folder + "racer.json", 65536, manifest, error))
        return racer_finish(state, -2, error);
    if (!hash_matches(manifest, entry.manifestSha256) || manifest.size() != entry.manifestBytes)
        return racer_finish(state, -2, "racer.json does not match the racer list.");
    NativeRacerEntry parsed;
    char modelSha[65], portraitSha[65], detail[256] = {0};
    size_t modelBytes = 0, portraitBytes = 0;
    if (!NativeRacerLibrary_ParseManifest(reinterpret_cast<const char *>(manifest.data()), manifest.size(), &parsed,
                                          modelSha, portraitSha, &modelBytes, &portraitBytes, detail, sizeof detail))
        return racer_finish(state, -2, std::string("racer.json: ") + detail);
    if (std::strcmp(parsed.id, entry.id) != 0)
        return racer_finish(state, -2, "racer.json is for another racer.");
    if (!https_get(kRacerHost, folder + parsed.modelPath, NATIVE_RACER_MODEL_MAX_BYTES, model, error) ||
        !https_get(kRacerHost, folder + parsed.portraitPath, 4096, portrait, error))
        return racer_finish(state, -2, error);
    if (!NativeRacerLibrary_Install(racersDir.c_str(), entry.folder, manifest.data(), manifest.size(), model.data(),
                                    model.size(), portrait.data(), portrait.size(), detail, sizeof detail))
        return racer_finish(state, -2, detail);
    racer_finish(state, 1, "");
}
}

extern "C" int NativeRacerCatalogue_StartRefresh(void)
{
    auto &state = racer_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0;
    try { std::thread(racer_refresh_worker).detach(); return 1; }
    catch (const std::exception &) { state.state = -1; return 0; }
}
extern "C" int NativeRacerCatalogue_PollRefresh(NativeRacerCatalogueEntry *out, int max, int *count, char *error, size_t errorSize)
{
    auto &state = racer_refresh_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state == 1 && out && count)
    {
        *count = std::min<int>(max, (int)state.entries.size());
        std::copy(state.entries.begin(), state.entries.begin() + *count, out);
    }
    return racer_poll(state, error, errorSize);
}
extern "C" int NativeRacerCatalogue_StartInstall(const NativeRacerCatalogueEntry *entry, const char *racersDir)
{
    if (!entry || !racersDir || !*racersDir || !std::memchr(entry->folder, 0, sizeof entry->folder) ||
        !std::memchr(entry->id, 0, sizeof entry->id) || !std::memchr(entry->manifestSha256, 0, sizeof entry->manifestSha256))
        return 0;
    auto &state = racer_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0;
    try { std::thread(racer_install_worker, *entry, std::string(racersDir)).detach(); return 1; }
    catch (const std::exception &) { state.state = -1; return 0; }
}
extern "C" int NativeRacerCatalogue_PollInstall(char *error, size_t errorSize)
{
    auto &state = racer_install_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    return racer_poll(state, error, errorSize);
}

/* A racer's portrait before it is installed: racer.json, checked against the
   racer list, names the portrait and its hash; the portrait is checked against
   that and kept in cacheDir as <id>-<first 16 of the racer.json hash>.bin, so
   a racer seen once shows offline and an updated racer is fetched again. */
namespace {
struct PreviewJobState
{
    std::mutex mutex;
    int state = -1;
    std::string id;
    std::vector<unsigned char> portrait;
};
PreviewJobState &preview_state() { static auto *state = new PreviewJobState(); return *state; }

void preview_finish(const std::string &id, int result, std::vector<unsigned char> portrait = {})
{
    auto &state = preview_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.id = id;
    state.portrait = std::move(portrait);
    state.state = result;
}

bool portrait_valid(const std::vector<unsigned char> &bytes)
{
    if (bytes.size() != NATIVE_RACER_PORTRAIT_BYTES || std::memcmp(bytes.data(), "CDP1", 4) != 0) return false;
    uint16_t w = 0, h = 0;
    std::memcpy(&w, &bytes[4], 2);
    std::memcpy(&h, &bytes[6], 2);
    return w == NATIVE_RACER_PORTRAIT_W && h == NATIVE_RACER_PORTRAIT_H;
}

void preview_worker(NativeRacerCatalogueEntry entry, std::string cacheDir)
{
    const std::string id = entry.id;
    const std::string cacheFile = join_path(cacheDir, (id + "-" + std::string(entry.manifestSha256).substr(0, 16) + ".bin").c_str());
    {
        std::ifstream file(cacheFile, std::ios::binary);
        std::vector<unsigned char> cached(NATIVE_RACER_PORTRAIT_BYTES + 1);
        file.read(reinterpret_cast<char *>(cached.data()), static_cast<std::streamsize>(cached.size()));
        cached.resize(static_cast<size_t>(file.gcount()));
        if (portrait_valid(cached)) return preview_finish(id, 1, std::move(cached));
    }
    const std::string folder = std::string(kRacerRoot) + "racers/" + entry.folder + "/";
    std::vector<unsigned char> manifest, portrait;
    std::string error;
    if (!https_get(kRacerHost, folder + "racer.json", 65536, manifest, error) ||
        !hash_matches(manifest, entry.manifestSha256) || manifest.size() != entry.manifestBytes)
        return preview_finish(id, -2);
    NativeRacerEntry parsed;
    char modelSha[65], portraitSha[65], detail[256] = {0};
    size_t modelBytes = 0, portraitBytes = 0;
    if (!NativeRacerLibrary_ParseManifest(reinterpret_cast<const char *>(manifest.data()), manifest.size(), &parsed,
                                          modelSha, portraitSha, &modelBytes, &portraitBytes, detail, sizeof detail) ||
        std::strcmp(parsed.id, entry.id) != 0)
        return preview_finish(id, -2);
    if (!https_get(kRacerHost, folder + parsed.portraitPath, 4096, portrait, error) || portrait.size() != portraitBytes ||
        !hash_matches(portrait, portraitSha) || !portrait_valid(portrait))
        return preview_finish(id, -2);
    // The cache is a convenience: a failed write still shows the portrait.
    const size_t slash = cacheDir.find_last_of("/\\");
    if ((slash == std::string::npos || make_directory(cacheDir.substr(0, slash))) && make_directory(cacheDir))
    {
        const std::string part = cacheFile + ".part";
        if (!write_file(part, portrait) || !replace_file(part, cacheFile)) std::remove(part.c_str());
    }
    preview_finish(id, 1, std::move(portrait));
}
}

extern "C" int NativeRacerCatalogue_StartPreview(const NativeRacerCatalogueEntry *entry, const char *cacheDir)
{
    if (!entry || !cacheDir || !*cacheDir || !std::memchr(entry->folder, 0, sizeof entry->folder) ||
        !std::memchr(entry->id, 0, sizeof entry->id) || !std::memchr(entry->manifestSha256, 0, sizeof entry->manifestSha256))
        return 0;
    auto &state = preview_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0;
    try { std::thread(preview_worker, *entry, std::string(cacheDir)).detach(); return 1; }
    catch (const std::exception &) { state.state = -1; return 0; }
}
extern "C" int NativeRacerCatalogue_PollPreview(char id[37], unsigned char portrait[NATIVE_RACER_PORTRAIT_BYTES])
{
    auto &state = preview_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    const int result = state.state;
    if (result == 0 || result == -1) return result;
    if (id) std::snprintf(id, 37, "%s", state.id.c_str());
    if (result == 1 && portrait) std::memcpy(portrait, state.portrait.data(), NATIVE_RACER_PORTRAIT_BYTES);
    state.state = -1;
    return result;
}
/* Track pictures (include/platform/native_track_picture.h): Saphi's thumbnail
   for one track, decoded and cached as "CTP1", the media id, then the texels. */
namespace {
struct PictureJobState
{
    std::mutex mutex;
    int state = -1, trackID = 0;
    std::vector<uint16_t> texels;
};
PictureJobState &picture_state() { static auto *state = new PictureJobState(); return *state; }

void picture_finish(int trackID, int result, std::vector<uint16_t> texels = {})
{
    auto &state = picture_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.trackID = trackID;
    state.texels = std::move(texels);
    state.state = result;
}

void picture_worker(int trackID, int mediaID, std::string path, std::string cacheDir)
{
    const std::string cacheFile = join_path(cacheDir, (std::to_string(trackID) + ".bin").c_str());
    const size_t texelBytes = NATIVE_TRACK_PICTURE_TEXELS * sizeof(uint16_t);
    std::vector<uint16_t> texels(NATIVE_TRACK_PICTURE_TEXELS);
    {
        std::ifstream file(cacheFile, std::ios::binary);
        std::vector<unsigned char> cached(8 + texelBytes + 1);
        file.read(reinterpret_cast<char *>(cached.data()), static_cast<std::streamsize>(cached.size()));
        int32_t cachedID = 0;
        std::memcpy(&cachedID, &cached[4], 4);
        if (file.gcount() == static_cast<std::streamsize>(8 + texelBytes) && !std::memcmp(cached.data(), "CTP1", 4) &&
            (mediaID == 0 || cachedID == mediaID))
        {
            std::memcpy(texels.data(), &cached[8], texelBytes);
            return picture_finish(trackID, 1, std::move(texels));
        }
    }
    const std::string prefix = "/api/v3/tracks/" + std::to_string(trackID) + "/downloads/";
    if (mediaID <= 0 || path.compare(0, prefix.size(), prefix) != 0) return picture_finish(trackID, -2);
    std::vector<unsigned char> jpeg;
    std::string error;
    if (!https_get(kSaphiHost, path, NATIVE_TRACK_PICTURE_MAX_BYTES, jpeg, error) ||
        !NativeTrackPicture_Decode(jpeg.data(), jpeg.size(), texels.data()))
        return picture_finish(trackID, -2);
    // The cache is a convenience: a failed write still shows the picture.
    const size_t slash = cacheDir.find_last_of("/\\");
    if ((slash == std::string::npos || make_directory(cacheDir.substr(0, slash))) && make_directory(cacheDir))
    {
        std::vector<unsigned char> bytes(8 + texelBytes);
        const int32_t id = mediaID;
        std::memcpy(bytes.data(), "CTP1", 4);
        std::memcpy(&bytes[4], &id, 4);
        std::memcpy(&bytes[8], texels.data(), texelBytes);
        const std::string part = cacheFile + ".part";
        if (!write_file(part, bytes) || !replace_file(part, cacheFile)) std::remove(part.c_str());
    }
    picture_finish(trackID, 1, std::move(texels));
}
}

extern "C" int NativeTrackPicture_Start(int trackID, int mediaID, const char *path, const char *cacheDir)
{
    if (trackID <= 0 || !cacheDir || !*cacheDir) return 0;
    auto &state = picture_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.state != -1) return 0;
    state.state = 0;
    try { std::thread(picture_worker, trackID, mediaID, std::string(path ? path : ""), std::string(cacheDir)).detach(); return 1; }
    catch (const std::exception &) { state.state = -1; return 0; }
}
extern "C" int NativeTrackPicture_Poll(int *trackID, uint16_t out[NATIVE_TRACK_PICTURE_TEXELS])
{
    auto &state = picture_state();
    std::lock_guard<std::mutex> guard(state.mutex);
    const int result = state.state;
    if (result == 0 || result == -1) return result;
    if (trackID) *trackID = state.trackID;
    if (result == 1 && out) std::copy(state.texels.begin(), state.texels.end(), out);
    state.state = -1;
    return result;
}
#endif // CTR_CUSTOM_PACKAGES
