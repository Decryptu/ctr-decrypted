#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# The same websocketpp and asio settings as the game's ap_net (CMakeLists.txt).
flags=(-m32 -std=c++17 -O0 -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STL_=1 -D_WEBSOCKETPP_CPP11_THREAD_=1
       -Iap/vendor/wswrap/include -Iap/vendor/websocketpp -Iap/vendor/asio/include
       -Iap/vendor/apclientpp -Iap/vendor/json/include)
libs=(-lssl -lcrypto -lz -pthread)
case "$(uname -s)" in
	MINGW*|MSYS*) flags+=(-D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN); libs+=(-lws2_32 -lmswsock -lcrypt32) ;;
esac
g++ "${flags[@]}" -DWSWRAP_WITH_SSL tools/security/transport-probe.cpp "${libs[@]}" -o "$work/transport"
python3 tools/security/test-transport.py "$work/transport"
g++ "${flags[@]}" tools/security/cache-probe.cpp "${libs[@]}" -o "$work/cache"
XDG_CACHE_HOME="$work/cache-root" "$work/cache"
