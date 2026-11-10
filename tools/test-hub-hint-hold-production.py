"""A3-12: the hub stream's hook points for the mask hint hold.

tools/test-hub-hint-hold.c runs ap/ap_hub_hint_hold.h against a model of the
hub stream and the talking mask. This pins that the engine calls it at the
places the model assumes, in that order, and only in AP builds. It cannot run
the engine: the mask on screen needs a played hub. Run from the repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
hub = (root / "game/LOAD/LOAD_Hub.c").read_text(encoding="utf-8")
level = (root / "game/LOAD/LOAD_Level.c").read_text(encoding="utf-8")
funcs = (root / "include/functions.h").read_text(encoding="utf-8")
hooks = (root / "ap/ap_hooks.h").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at) + 3]


main = body(hub, "void LOAD_Hub_Main(struct BigHeader *bigfilePtr)\n{")
read = body(hub, "void LOAD_Hub_ReadFile(struct BigHeader *bigfile, int levID, int packID)\n{")
mask = body(level, "void LOAD_TalkingMask(int packID, int maskID)\n{")

# The hold itself and the eviction hook, AP only.
assert "#ifdef CTR_AP\n// A3-12: hub streaming waits for a running mask hint (ap/ap_hub_hint_hold.h).\nstatic ap_hub_hint_hold s_apHubHintHold = {-1, -1, 0, -1};" in hub
assert '#include "ap_hub_hint_hold.h"' in hooks
assert "#ifdef CTR_AP\nvoid LOAD_Hub_ApMaskEvicts(int packID);\n#endif" in funcs
evicts = body(hub, "void LOAD_Hub_ApMaskEvicts(int packID)\n{")
assert "ap_hub_hint_hold_evict(&s_apHubHintHold, gGT->levID_in_each_mempack[packID], gGT->levelID);" in evicts

# LOAD_TalkingMask records the hub before it marks the pack empty.
at_evict = mask.index("LOAD_Hub_ApMaskEvicts(packID);")
assert at_evict < mask.index("GAME_TRACKER->levID_in_each_mempack[packID] = -1;") < mask.index("MEMPACK_ClearLowMem();")
assert "#ifdef CTR_AP" in mask[at_evict - 120:at_evict]

# LOAD_Hub_Main: a held read goes first, while the loader is idle and before the
# player loop, then a held swap, only into a hub; a corridor swap during a hint
# is remembered by the hold, not through retail's cutscene swap flag; a corridor
# read is held.
PAIR = "LOAD_HUB_MEMPACK_PAIR_INDEX_SUM - "
loop = main.index("for (i = 0; i < GAME_TRACKER->numPlyrCurrGame; i++)")
release = main.index("ap_hub_hint_hold_release(&s_apHubHintHold, sdata->AkuAkuHintState, gGT->levelID);")
assert main.index("if (GAME_LOADING_STAGE == LOAD_IDLE)") < release < loop
assert "LOAD_Hub_ReadFile(bigfilePtr, heldLev, " + PAIR + "gGT->activeMempackIndex);\n\t\t\treturn;" in main
swap_release = main.index("ap_hub_hint_hold_swap_release(&s_apHubHintHold, sdata->AkuAkuHintState, gGT->levelID,")
assert release < swap_release < loop
assert "gGT->levID_in_each_mempack[" + PAIR + "gGT->activeMempackIndex]))" in main[swap_release:swap_release + 220]
assert "gGT->bool_AdvHub_NeedToSwapLEV = 0;\n\t\t\tLOAD_Hub_SwapNow();\n\t\t\treturn;" in main[swap_release:swap_release + 320]
swap = main.index("if (ap_hub_hint_hold_swap(&s_apHubHintHold, sdata->AkuAkuHintState, GAME_TRACKER->levelID,\n\t\t\t\t                          GAME_TRACKER->levID_in_each_mempack[" + PAIR + "GAME_TRACKER->activeMempackIndex]))\n\t\t\t\t{\n\t\t\t\t\tcontinue;")
assert swap < main.index("GAME_TRACKER->bool_AdvHub_NeedToSwapLEV = 0;\n\t\t\t\tLOAD_Hub_SwapNow();")
assert main.count("bool_AdvHub_NeedToSwapLEV = 1") == 0
held = main.index("if (ap_hub_hint_hold_read(&s_apHubHintHold, sdata->AkuAkuHintState, levID, GAME_TRACKER->levelID))")
assert main.index("if (currLevelID < LOAD_ADV_HUB_COUNT)") < held < main.index("LOAD_Hub_ReadFile(bigfilePtr, levID, " + PAIR + "GAME_TRACKER->activeMempackIndex);")

# Only the corridor path holds: LOAD_Hub_ReadFile itself is unchanged, so the
# cutscene thread and the character swap keep their own read handshakes.
assert "ap_hub_hint_hold" not in read and "AkuAkuHintState" not in read
for gate in (main[max(0, release - 400):release], main[swap - 200:swap], main[held - 120:held]):
    assert "#ifdef CTR_AP" in gate
print("PASS: hub stream hint hold production text")
