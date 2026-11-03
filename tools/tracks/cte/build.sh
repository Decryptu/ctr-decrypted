#!/bin/bash
# Build cte_headless.exe: CrashTeamEditor (MIT, github.com/mateusfavarin/CrashTeamEditor) at a pinned
# commit, with crashteameditor.patch and headless.cpp (load an OBJ with its presets, save LEV + VRM).
# Run from MSYS2 MinGW32:
#   MSYSTEM=MINGW32 /c/msys64/usr/bin/bash.exe -lc "bash tools/tracks/cte/build.sh C:/t/cte"
# The argument is the work folder (cloned on the first run); the exe lands in it.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${1:-C:/t/cte}
COMMIT=37ba52bf9a9bb155532d071a525184e94f97d472

if [ ! -d "$WORK/.git" ]; then
	git clone -q https://github.com/mateusfavarin/CrashTeamEditor "$WORK"
	git -C "$WORK" -c advice.detachedHead=false checkout -q $COMMIT
	git -C "$WORK" submodule update -q --init --recursive
	git -C "$WORK" apply --ignore-whitespace "$HERE/crashteameditor.patch"
fi
cp "$HERE/headless.cpp" "$WORK/headless.cpp"
cd "$WORK"

mkdir -p glfw-build obj
[ -f glfw-build/src/libglfw3.a ] || (cmake -S third_party/glfw -B glfw-build -G Ninja -DCMAKE_C_COMPILER=gcc -DGLFW_BUILD_EXAMPLES=OFF \
	-DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF >/dev/null && cmake --build glfw-build -j8 >/dev/null)
PYINC=$(python -c "import sysconfig;print(sysconfig.get_paths()['include'])")
# UNICODE and -fpermissive: the Windows file dialogs CTE links expect them under MinGW
FLAGS="-O2 -std=c++20 -DNDEBUG -DCTE_EMBEDDED_BUILD -iquote src -Ithird_party/imgui -Ithird_party/imgui/backends -Ithird_party/glfw/include \
	-Ithird_party/portable-file-dialogs -Ithird_party/json/include -Ithird_party/pybind11/include -I$PYINC -Ithird_party/glm/glm -Ithird_party/glm \
	-Ithird_party/stb -Isrc/manual_third_party -DNOMINMAX -D_USE_MATH_DEFINES -DUNICODE -D_UNICODE -fpermissive"
SRCS="$(ls src/*.cpp | grep -v src/main.cpp) third_party/imgui/imgui.cpp third_party/imgui/imgui_demo.cpp third_party/imgui/imgui_draw.cpp \
	third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_glfw.cpp \
	third_party/imgui/backends/imgui_impl_opengl3.cpp third_party/imgui/misc/cpp/imgui_stdlib.cpp python_bindings/cte_bindings.cpp headless.cpp"
OBJS=""
for s in $SRCS; do
	o=obj/$(echo $s | tr '/' '_').o
	OBJS="$OBJS $o"
	[ -f $o -a $o -nt $s ] || g++ $FLAGS -c $s -o $o &
done
wait
gcc -O2 -Isrc/manual_third_party -c src/manual_third_party/glad.c -o obj/glad.o
g++ -o cte_headless.exe $OBJS obj/glad.o glfw-build/src/libglfw3.a -lopengl32 -lgdi32 -lcomdlg32 -lole32 -lshell32 -luuid -lpython3.14 \
	-static-libgcc -static-libstdc++
echo "built $WORK/cte_headless.exe"
