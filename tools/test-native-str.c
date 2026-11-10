// cc -m32 -std=gnu11 -DCTR_NATIVE -I include -I . tools/test-native-str.c -o /tmp/test-native-str
// Production STR reader/decoder, with only file, audio and GPU boundaries stubbed.
#include <assert.h>
#include <stdlib.h>
#include "platform/native_str.c"
static FILE *input;
static int fed, uploads, textures, destroyed;
FILE *NativeAssets_OpenHost(const char *p, const char *m) { (void)p; (void)m; FILE *f=input; input=NULL; return f; }
FILE *NativeAssets_OpenHostBigfile(const char *m) { (void)m; return NULL; }
int NativeDiscImage_FindFile(const char *p, struct NativeDiscImageFile *f) { (void)p; (void)f; return 0; }
int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *f, u32 s, u32 n, void *p)
{ (void)f; (void)s; (void)n; (void)p; return 0; }
int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *f, u32 s, u32 n, void *p)
{ return NativeDiscImage_ReadRawSectors(f,s,n,p); }
int NativeAudio_FeedInterleavedXASector(const void *s, int n)
{ const u8 *p=s; assert(n==2336); if (p[2]&4) fed++; return 0; }
TextureID NativeRenderer_CreateStreamingTexture(int w, int h) { assert(w==16 && h==16); return ++textures; }
void NativeRenderer_DestroyStreamingTexture(TextureID t) { if(t) destroyed++; }
void NativeRenderer_UpdateStreamingTexture(TextureID t, int w, int h, const u8 *pixels)
{ assert(t && w==16 && h==16); for (int i=0;i<w*h;i++) { assert(pixels[4*i]==128 && pixels[4*i+1]==128 && pixels[4*i+2]==128 && pixels[4*i+3]==255); } uploads++; }
int LoadImage(RECT16 *r, void *p) { (void)r; (void)p; return 0; }
void NativeRenderer_UpdateVRAM(void) {}
static void le16(u8 *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void le32(u8 *p, unsigned v) { le16(p,v); le16(p+2,v>>16); }
static void bits(u8 *p, int *offset, int value, int count)
{
	for (int b=count-1;b>=0;b--) { int k=(*offset)++; int byte=(k/16)*2; unsigned word=p[byte]|(p[byte+1]<<8); word|=((value>>b)&1)<<(15-(k%16)); le16(p+byte,word); }
}
static void record(FILE *f, int size, int index, int chunks, int frame, int damaged)
{
	u8 sector[2352]={0}; int off=size==2352?24:size==2336?8:0;
	u8 *p=sector+off;
	le32(p,0x80010160); le16(p+4,index); le16(p+6,chunks); le32(p+8,frame);
	le32(p+12,chunks>1?3000:18); le16(p+16,16); le16(p+18,16);
	p+=32; le16(p+2,damaged?0:0x3800); le16(p+4,1); le16(p+6,2);
	int bit=64; for(int i=0;i<6;i++) { bits(p,&bit,0,10); bits(p,&bit,2,2); }
	assert(fwrite(sector,1,size,f)==(size_t)size);
}
int main(void)
{
	for (int size=2048; size<=2352; size+=(size==2048?288:16)) {
		input=tmpfile(); assert(input);
		if(size!=2048) { u8 xa[2352]={0}; int off=size==2352?16:0; xa[off]=xa[off+4]=1; xa[off+1]=xa[off+5]=1; xa[off+2]=xa[off+6]=4; fwrite(xa,1,size,input); }
		record(input,size,0,2,1,0); // missing chunk 1: resynchronize on next frame
		record(input,size,0,1,2,1); // bad bitstream: recover on frame 3
		record(input,size,0,1,3,0);
		record(input,size,0,1,4,0);
		rewind(input);
		assert(NativeSTR_StartScrapbook());
		assert(NativeSTR_UploadNextFrameToTexture()); assert(NativeSTR_GetSourceFrame()==3);
		assert(NativeSTR_UploadNextFrameToTexture()); assert(NativeSTR_GetSourceFrame()==4);
		assert(!NativeSTR_UploadNextFrameToTexture());
		NativeSTR_Stop(); assert(!NativeSTR_GetTexture() && !NativeSTR_GetWidth());
	}
	assert(uploads==6 && textures==3 && destroyed==3 && fed==2);
	// Truncated and impossible frame headers never produce a picture.
	input=tmpfile(); assert(input); fputs("truncated",input); rewind(input);
	assert(!NativeSTR_StartScrapbook());
	NativeSTR_Stop();
	// Bounded damaged bitstreams must not read outside the frame or overflow.
	u32 rng=1;
	for (int trial=0; trial<1000; trial++) {
		s_str.width=s_str.height=16; s_str.frameSize=2016;
		for (int i=0; i<s_str.frameSize; i++) { rng=rng*1664525u+1013904223u; s_str.frameData[i]=rng>>24; }
		le16(s_str.frameData+2,0x3800); le16(s_str.frameData+4,63); le16(s_str.frameData+6,2);
		NativeSTR_DecodeFrame();
	}
	return 0;
}
