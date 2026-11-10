#ifndef NATIVE_STR_H
#define NATIVE_STR_H

#include <macros.h>

s32 NativeSTR_StartTrackPreviewFromBigfileSector(s32 bigfileSector, s32 frameCount);
s32 NativeSTR_StartScrapbook(void);
void NativeSTR_Stop(void);
s32 NativeSTR_UploadNextFrameToTexture(void);
u32 NativeSTR_GetTexture(void);
s32 NativeSTR_GetHeight(void);
s32 NativeSTR_GetWidth(void);
u32 NativeSTR_GetSourceFrame(void);
s32 NativeSTR_UploadNextFrame(s32 dstX, s32 dstY);

#endif
