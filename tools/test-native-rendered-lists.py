"""Run the production retry-list writers and frame resets with small host seams."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
overlay = (root / "game/226/226_00_DrawLevelOvr1P.c").read_text()
resets = (root / "game/CTR/CTR_RenderLists.c").read_text()


def function(source, name):
    start = re.search(r"^(?:static )?(?:void|int|struct QuadBlock \*\*)\s*" + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert start, name
    body = source.index("{", start.start())
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start.start():end]


fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <platform/native_rendered_list.h>
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks_policy.h>
#endif
typedef uint32_t u32;
struct QuadBlock { int value; } block;
struct Slot { void *bspListStart; struct QuadBlock **ptrQuadBlocksRendered; };
struct RenderList { struct Slot list[5]; void *bspListStart_FullDynamic; void *ptrQuadBlocksRendered_FullDynamic; };
struct GameTracker { struct RenderList LevRenderLists[4]; } gt;
static struct { void *ptrRenderedQuadblockDestination_forEachPlayer[4], *ptrRenderedQuadblockDestination_again[4]; } data;
static struct { struct QuadBlock *quadBlocksRendered[256]; } sdata_static;
static struct { u32 renderedOverflowPtr32; } scratch;
#define DrawLevelOvr1P_Scratch() (&scratch)
static struct QuadBlock **sDrawLevelOvr1P_RenderedOverflowBase;
static struct QuadBlock *sDrawLevelOvrNativeRendered[CTR_NATIVE_RENDERED_LIST_PLAYERS][CTR_NATIVE_RENDERED_LIST_SLOTS];
static struct QuadBlock **sDrawLevelOvr1P_RenderedOverflowEnd;
static unsigned long refused, peak;
static void CustomTrackDiag_NoteRenderedListFull(void) { refused++; }
static void CustomTrackDiag_NoteRenderedUse(unsigned long n) { if (n > peak) peak = n; }
'''
names = ["DrawLevelOvr_InstallNativeRenderedStorage", "DrawLevelOvr1P_NativeRenderedSlotsFit",
         "DrawLevelOvr1P_RenderedListEnd", "DrawLevelOvr1P_SetRenderedListCursor",
         "DrawLevelOvr1P_SetRenderedOverflowBase", "DrawLevelOvr_ClearRenderedOverflowBase",
         "DrawLevelOvr1P_GetRenderedOverflowBase", "DrawLevelOvr1P_GetRenderedListCursor",
         "DrawLevelOvr1P_AppendRenderedQuadBlock", "DrawLevelOvr1P_TerminateRenderedListCursor"]
production = "\n".join(function(overlay, name) for name in names)
production += "\n" + "\n".join(function(resets, name) for name in
                                ("CTR_ClearRenderLists_1P2P", "CTR_ClearRenderLists_3P4P"))

checks = r'''
static void select_player(int p) {
    DrawLevelOvr1P_SetRenderedOverflowBase(data.ptrRenderedQuadblockDestination_forEachPlayer[p]);
    DrawLevelOvr1P_SetRenderedListCursor(DrawLevelOvr1P_GetRenderedOverflowBase());
}
int main(void) {
    const unsigned long cap = CTR_NATIVE_RENDERED_LIST_SLOTS;
    assert(cap == 1024);
    // Both pointer families and every bucket must use the player's own array.
    for (int n = 1; n <= 4; n++) {
        memset(&gt, 0xA5, sizeof gt);
        if (n < 3) CTR_ClearRenderLists_1P2P(&gt, n);
        else CTR_ClearRenderLists_3P4P(&gt, n);
        for (int p = 0; p < 4; p++) {
            assert(data.ptrRenderedQuadblockDestination_forEachPlayer[p] == sDrawLevelOvrNativeRendered[p]);
            assert(data.ptrRenderedQuadblockDestination_again[p] == sDrawLevelOvrNativeRendered[p]);
        }
        for (int p = 0; p < n; p++) {
            assert(sDrawLevelOvrNativeRendered[p][0] == NULL);
            for (int b = 0; b < (n < 3 ? 5 : 4); b++) {
                assert(gt.LevRenderLists[p].list[b].ptrQuadBlocksRendered == sDrawLevelOvrNativeRendered[p]);
                assert(gt.LevRenderLists[p].list[b].bspListStart == NULL);
            }
            // The empty-bucket reset must not clear another player's list.
            sDrawLevelOvrNativeRendered[p][0] = &block;
            DrawLevelOvr_ClearRenderedOverflowBase(p);
            assert(sDrawLevelOvrNativeRendered[p][0] == NULL);
        }
    }
    // Deliberately exceed the old 64-pointer slices and the new capacity.
    for (int p = 0; p < 4; p++) {
        struct QuadBlock *before[4][CTR_NATIVE_RENDERED_LIST_SLOTS];
        memset(sDrawLevelOvrNativeRendered, 0xA5, sizeof sDrawLevelOvrNativeRendered);
        memcpy(before, sDrawLevelOvrNativeRendered, sizeof before);
        refused = peak = 0;
        select_player(p);
        for (unsigned long i = 0; i < cap - 1; i++) DrawLevelOvr1P_AppendRenderedQuadBlock(&block);
        assert(DrawLevelOvr1P_GetRenderedListCursor() == sDrawLevelOvrNativeRendered[p] + cap - 1);
        for (int i = 0; i < 200; i++) DrawLevelOvr1P_AppendRenderedQuadBlock(&block);
        DrawLevelOvr1P_TerminateRenderedListCursor();
        for (unsigned long i = 0; i < cap - 1; i++) assert(sDrawLevelOvrNativeRendered[p][i] == &block);
        assert(sDrawLevelOvrNativeRendered[p][cap - 1] == NULL);
        for (int other = 0; other < 4; other++) if (other != p)
            assert(memcmp(before[other], sDrawLevelOvrNativeRendered[other], sizeof before[other]) == 0);
#ifdef CTR_CUSTOM_TRACKS
        assert(refused == 200 && peak == cap - 1);
        assert(CTR_CT_RENDERED_QUADBLOCK_SLOTS == cap);
#endif
        // Invalid scratch cursors cannot write a terminator or an entry.
        memcpy(before, sDrawLevelOvrNativeRendered, sizeof before);
        uintptr_t base = (uintptr_t)sDrawLevelOvrNativeRendered[p];
        uintptr_t bad[] = {0, base - sizeof(void *), base + 1,
                           base + cap * sizeof(void *), base + (cap + 1) * sizeof(void *)};
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            DrawLevelOvr1P_SetRenderedListCursor((struct QuadBlock **)bad[i]);
            DrawLevelOvr1P_AppendRenderedQuadBlock(&block);
            DrawLevelOvr1P_TerminateRenderedListCursor();
            assert(memcmp(before, sDrawLevelOvrNativeRendered, sizeof before) == 0);
        }
    }
    // Stale retail bases are rejected rather than treated as native storage.
    DrawLevelOvr1P_SetRenderedOverflowBase(sdata_static.quadBlocksRendered);
    DrawLevelOvr1P_SetRenderedListCursor(sdata_static.quadBlocksRendered);
    DrawLevelOvr1P_AppendRenderedQuadBlock(&block);
    DrawLevelOvr1P_TerminateRenderedListCursor();
    assert(sdata_static.quadBlocksRendered[0] == NULL);
    sDrawLevelOvr1P_RenderedOverflowBase = NULL;
    assert(DrawLevelOvr1P_GetRenderedOverflowBase() == sDrawLevelOvrNativeRendered[0]);
    // A new frame after overflow can append and consume a fresh list.
    for (int n = 1; n <= 4; n++) {
        if (n < 3) CTR_ClearRenderLists_1P2P(&gt, n);
        else CTR_ClearRenderLists_3P4P(&gt, n);
        for (int p = 0; p < n; p++) {
            assert(sDrawLevelOvrNativeRendered[p][0] == NULL);
            select_player(p);
            DrawLevelOvr1P_AppendRenderedQuadBlock(&block);
            DrawLevelOvr1P_TerminateRenderedListCursor();
            assert(sDrawLevelOvrNativeRendered[p][0] == &block);
            assert(sDrawLevelOvrNativeRendered[p][1] == NULL);
        }
    }
    puts("native retry lists: capacity, overflow, isolation, invalid cursors, diagnostics and frame reset OK");
}
'''

# Keep the storage declarations and 1P entry wiring tied to the production file.
assert fixture[fixture.index("static struct QuadBlock *sDrawLevelOvrNativeRendered"):].splitlines()[0] in overlay
entry = function(overlay, "DrawLevelOvr1P")
assert "DrawLevelOvr_InstallNativeRenderedStorage();" in entry
assert "DrawLevelOvr1P_SetRenderedOverflowBase(sDrawLevelOvrNativeRendered[0]);" in entry
for players in (2, 3, 4):
    source = (root / f"game/{225 + players}/{225 + players}_00_DrawLevelOvr{players}P.c").read_text()
    assert "data.ptrRenderedQuadblockDestination_forEachPlayer[playerIndex]" in source
    assert "DrawLevelOvr1P_SetViewportScratchContext(" in source

with tempfile.TemporaryDirectory(prefix="ctr-rendered-lists-") as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / ("test.exe" if os.name == "nt" else "test")
    src.write_text(fixture + production + checks)
    for custom in (False, True):
        command = ["cc", "-m32", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wno-unused-function",
                   "-DCTR_NATIVE", "-I", str(root / "include"), str(src), "-o", str(exe)]
        if custom:
            command.append("-DCTR_CUSTOM_TRACKS")
        if os.name != "nt":
            command.append("-fsanitize=undefined")
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
