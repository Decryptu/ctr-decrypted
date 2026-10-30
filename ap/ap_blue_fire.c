#ifdef CTR_AP

#include <common.h>

#include "ap_blue_fire.h"
#include "ap_capability.h"

// The visual half of the tier-3 mechanical capstone; grant and handling
// behaviour live at the relevant physics decisions. The palette swap itself is
// game/Vehicle/VehBlueFire.c, shared with the Extras "Blue Fire At USF" option.
int AP_BlueFireWanted(const struct Driver *driver)
{
	return driver->reserves > 0 &&
	       AP_CapabilityBoostTier() == AP_CAP_BOOST_BLUEFIRE &&
	       (int)driver->fireSpeedCap > (int)driver->const_SacredFireSpeed;
}

#endif
