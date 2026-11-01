#ifndef AP_SEED_PROMPT_H
#define AP_SEED_PROMPT_H

// The missing-tracks prompt on connecting to a content-plan room
// (ap/ap_seed_prompt.c).

struct RectMenu;
// The main-menu proc reports each frame the top-level menu is settled.
void AP_SeedPromptMainMenuSettled(struct RectMenu *menu);
// From RECTMENU_ProcessState: 1 when the prompt owns this frame.
int AP_SeedPromptMenuFrame(void);

#endif
