#ifndef AP_TITLE_ROO_H
#define AP_TITLE_ROO_H

// The title screen's Roo (ap/ap_title_roo.c).

struct Instance;
struct Model;

// Roo's podium dance model, its textures in the racer bank; read from the
// disc on the first call. NULL when that failed: the title keeps Crash.
struct Model *AP_TitleRoo_Model(void);
// A new title scene: Roo laughs again once it settles.
void AP_TitleRoo_Reset(void);
// Size Roo and loop his dance on his title instance, once per frame; settled
// is nonzero once the logo has landed, when he laughs once.
void AP_TitleRoo_Tick(struct Instance *inst, int settled);

#endif
