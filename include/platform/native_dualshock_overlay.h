#ifndef PLATFORM_NATIVE_DUALSHOCK_OVERLAY_H
#define PLATFORM_NATIVE_DUALSHOCK_OVERLAY_H

// Decodes the embedded DualShock RGBA image for the Ghost Replay input viewer.
// Caller frees the returned buffer with NativeDualshockOverlay_Free().
unsigned char *NativeDualshockOverlay_DecodeRGBA(int *outWidth, int *outHeight);
void NativeDualshockOverlay_Free(unsigned char *pixels);

#endif
