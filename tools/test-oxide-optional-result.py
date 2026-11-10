"""Compile actual Oxide win notification; entry/loss never invoke this hook."""
from pathlib import Path
import subprocess
import tempfile
import ap_hooks_unit

root = Path(__file__).resolve().parents[1]
source = ap_hooks_unit.read()
start = source.index("void AP_NotifyGoal(int oxideSecond)")
end = source.index("\n// ---------------------------------------------------------------------------", start)
function = source[start:end]
reward_source = (root / "game/222.c").read_text()
# The Oxide branch of the boss-race result: from the story bit through the
# goal notifications, up to the ending claim.
start = reward_source.index("\t\t\t\tif ((GAME_ADV_PROGRESS.rewards[ADV_PROGRESS_WORD_STORY] & ADV_REWARD_BEAT_OXIDE_FIRST_BOSS_MASK) == 0)")
end = reward_source.index("\n\t\t\t\tif (AP_GoalClaimOxideEnding())", start)
# The cut ends inside the CTR_AP block that closes after the claim.
reward = reward_source[start:end] + "\n#endif"
fixture = r'''
#include <assert.h>
/* Mode policy is exercised separately by test-adventure-checks-production.py. */
int AP_LocationChecksAllowed(void) { return 1; }
#include "ap_race_attempt_logic.h"
#include "ap_boss_won_flags.h"
#define AP_GOAL_BIT_OXIDE_FIRST 115
#define AP_GOAL_BIT_OXIDE_SECOND 116
static struct { int goal_oxide, oxide_1_optional; } ctr_cfg;
static int active, ap_oxide_first_beaten, ap_oxide_final_beaten;
static int first, final, arm, evaluate, wonRecords;
// #458: the persisted "Oxide's Challenge personally cleared" bit.
void ap_net_boss_won_record(unsigned bit) { assert(bit == AP_BOSS_WON_OXIDE_FIRST); wonRecords++; }
#define AA_OXIDE_SECOND_WIN_BOSS_ID 7
#define ADV_REWARD_OXIDE_FIRST_WIN_FLAGS 1
#define ADV_REWARD_OXIDE_SECOND_WIN_FLAGS 2
static struct { int bossID; } game, *raceGameTracker=&game;
#define ADV_REWARD_FIRST_BOSS_KEY 0x5e
#define ADV_PROGRESS_WORD_STORY 3
#define ADV_REWARD_BEAT_OXIDE_FIRST_BOSS_MASK 1
static struct { unsigned rewards[8]; } advStore;
#define GAME_ADV_PROGRESS advStore
static int bossRewards;
// Beating Oxide also unlocks him outside Adventure (include/namespace_Memcard.h).
#define GAME_UNLOCK_BIT_OXIDE 37
#define UNLOCK_ADV_BIT(words, bit) ((words)[(bit) >> 5] |= 1u << ((bit) & 0x1f))
static struct { struct { unsigned unlocks[2]; } gameProgress; } sdataStore, *sdata=&sdataStore;
#define GAME_PROGRESS (sdata->gameProgress)
int ctr_cfg_active(void) { return active; }
void AP_GoalArmLiveEvent(void) { arm++; }
void AP_NotifyAdvReward(int bit) {
 if (bit == 115) first++;
 else if (bit == 116) final++;
 else { assert(bit == game.bossID + ADV_REWARD_FIRST_BOSS_KEY); assert(!first && !final); bossRewards++; }
}
void AP_EvaluateGoal(void) { evaluate++; }
// #286: production reaches the freestanding decision through
// AP_RaceAttempt_ProducerBlocked with the live attempt latch; this fixture keeps
// the latch clear so the goal path under test behaves as before.
static int race_attempt_latched;
int AP_RaceAttempt_ProducerBlocked(int producerClass) {
 return AP_RaceAttempt_SuppressResultProducer(producerClass, race_attempt_latched);
}
'''
tests = r'''
int main(void) {
 for (active=0; active<2; active++)
 for (int goal=0; goal<4; goal++)
 for (int enabled=0; enabled<3; enabled++)
 for (int second=0; second<2; second++) {
  ctr_cfg.goal_oxide=goal; ctr_cfg.oxide_1_optional=enabled;
  first=final=arm=evaluate=ap_oxide_first_beaten=ap_oxide_final_beaten=wonRecords=0;
  AP_NotifyGoal(second);
  assert(first==(!second || (active && goal==2 && enabled)));
  assert(wonRecords==(active && (!second || (goal==2 && enabled))));
  assert(final==second && arm==1 && evaluate==1);
  assert(ap_oxide_first_beaten==!second && ap_oxide_final_beaten==second);
  first=final=arm=evaluate=ap_oxide_first_beaten=ap_oxide_final_beaten=0;
  sdata->gameProgress.unlocks[1]=0;
  game.bossID=second ? 7 : 6; bossRewards=0;
  RewardOxide();
  assert(bossRewards==1);
  assert(sdata->gameProgress.unlocks[1]==(1u << (37 - 32)));
  int skip = active && goal==2 && enabled;
  assert(first==1 && final==second);
  assert(arm==(second && !skip ? 2 : 1) && evaluate==arm);
  assert(ap_oxide_first_beaten==(!second || !skip));
  assert(ap_oxide_final_beaten==second);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "fixture.c", Path(tmp) / "fixture"
    src.write_text(fixture + function + "\nvoid RewardOxide(void) {\n" + reward + "\n}\n" + tests)
    subprocess.run(["cc", "-std=c99", "-DCTR_AP", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-I" + str(root / "ap"),
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 48 isolated win notifications plus 48 actual reward-path sequences, UBSan; no opt-in duplicate dispatch")
