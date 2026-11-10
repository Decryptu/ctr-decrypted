"""Production interleaved-XA queue and underrun checks."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / "platform/native_audio.c").read_text()
def function(name):
    start = re.search(r"^(?:internal )?(?:int|void) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert start, name
    a = source.index("{", start.start()); b = a + 1; depth = 1
    while depth:
        depth += (source[b] == "{") - (source[b] == "}"); b += 1
    return source[start.start():b]
production = "\n".join(function(n) for n in ["NativeAudio_IsXAAudioSector", "NativeAudio_BeginInterleavedXA", "NativeAudio_FeedInterleavedXASector", "NativeAudio_XaStreamingUnderrunNoLock"])
fixture = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <platform/native_audio.h>
#define internal static
#define XA_FORM2_SECTOR_SIZE 2336
#define NATIVE_AUDIO_XA_STREAM_SECTORS 64
#define NATIVE_AUDIO_FP_SHIFT 16
#define NATIVE_AUDIO_SAMPLE_RATE 44100
#define XA_SAMPLE_RATE_37800 37800
#define XA_SAMPLE_RATE_18900 18900
#define NATIVE_AUDIO_XA_ZIGZAG_INPUTS 6
#define NATIVE_AUDIO_XA_ZIGZAG_PHASES 7
struct NativeAudioXaStream { u8 *sectors; int sectorSize, sectorBase, streaming, sectorCapacity, channelFilter, nextSector, sectorCount, numChannels; u64 decodedFrames; };
static struct {
 struct NativeAudioXaStream xaStream;
 struct { int active, sampleRate, frameCount, categoryID, xaID, hasTrackIdentity, volumeLeft, volumeRight; u64 outputFrame, positionFp; u32 stepFp; } xa;
 SpuCommonAttr commonAttr;
} s_audio;
static int locked;
s32 NativeAudio_SpuInit(void) { return 1; }
static void NativeAudio_LockOutput(void) { assert(!locked); locked=1; }
static void NativeAudio_UnlockOutput(void) { assert(locked); locked=0; }
static void NativeAudio_CloseXANoLock(void) { free(s_audio.xaStream.sectors); memset(&s_audio,0,sizeof(s_audio)); }
static void NativeAudio_UpdateXAPositionFromOutputFrameNoLock(void) {}
'''
tests = r'''
static void header(u8 *p, int channel, int coding) { memset(p,0,2336); p[0]=1;p[1]=channel;p[2]=4;p[3]=coding;memcpy(p+4,p,4); }
int main(void) {
 u8 sector[2336];
 assert(!NativeAudio_BeginInterleavedXA(-1,0,0));
 for(int rate=0;rate<2;rate++) for(int stereo=0;stereo<2;stereo++) {
  assert(NativeAudio_BeginInterleavedXA(1,100,200));
  header(sector,2,stereo+rate*4); assert(!NativeAudio_FeedInterleavedXASector(sector,2336));
  header(sector,1,stereo+rate*4); assert(!NativeAudio_FeedInterleavedXASector(sector,2000));
  sector[4]=7; assert(!NativeAudio_FeedInterleavedXASector(sector,2336));sector[4]=1;
  assert(NativeAudio_FeedInterleavedXASector(sector,2336)==1);
  assert(s_audio.xa.sampleRate==(rate?18900:37800) && s_audio.xaStream.numChannels==stereo+1);
  assert(s_audio.xa.volumeLeft==100 && s_audio.xa.volumeRight==200);
  for(int i=1;i<64;i++) { sector[20]=i; assert(NativeAudio_FeedInterleavedXASector(sector,2336)==1); }
  assert(NativeAudio_FeedInterleavedXASector(sector,2336)==-1);
  s_audio.xaStream.nextSector=1;sector[20]=99;
  assert(NativeAudio_FeedInterleavedXASector(sector,2336)==1);
  assert(s_audio.xaStream.sectors[20]==99 && s_audio.xaStream.sectors[2336+20]==1);
  s_audio.xaStream.nextSector=s_audio.xaStream.sectorCount;
  s_audio.xa.outputFrame=0;s_audio.xaStream.decodedFrames=0; assert(NativeAudio_XaStreamingUnderrunNoLock());
  s_audio.xaStream.decodedFrames=100;assert(!NativeAudio_XaStreamingUnderrunNoLock());
  s_audio.xa.outputFrame=1000;assert(NativeAudio_XaStreamingUnderrunNoLock());
  header(sector,1,0x20);assert(!NativeAudio_FeedInterleavedXASector(sector,2336));
 }
 NativeAudio_CloseXANoLock(); assert(!s_audio.xaStream.sectors && !locked);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ctr-xa-") as tmp:
    c = Path(tmp) / "xa.c"
    exe = Path(tmp) / ("xa.exe" if os.name == "nt" else "xa")
    c.write_text(fixture + production + tests)
    flags = [] if os.name == "nt" else ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
    subprocess.run(["gcc", "-m32", "-O2", "-DCTR_NATIVE", "-I", str(root / "include"), *flags, str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("interleaved XA: filtering, bounded queue, wrap, underrun and reset passed")
