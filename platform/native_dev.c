#include "platform/native_dev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads the main render target (the frame at render scale) as bottom-up RGBA.
// Defined in platform/native_renderer.c. Returns a malloc'd buffer or NULL.
unsigned char *NativeRenderer_ReadMainTarget(int *width, int *height);
// Copies a VRAM rectangle (15-bit PSX pixels). Defined in native_renderer.c.
void NativeRenderer_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h);

typedef struct
{
	int loaded;
	int demoTrack;
	int fast;
	int hidden;
	int noItems;
	int botSpeed;
	const char *shotDir;
	int shotEvery;
	int shotFrom;
	int exitFrame;
	int dumpFrame;
	FILE *dump;
	int frame;
	int vramShot; // this frame was presented straight from VRAM (vramX..vramH)
	int vramX, vramY, vramW, vramH;
} NativeDevState;

static NativeDevState s_dev;

static int NativeDev_EnvInt(const char *name, int fallback)
{
	const char *value = getenv(name);
	return (value != NULL && *value != '\0') ? atoi(value) : fallback;
}

static NativeDevState *NativeDev_State(void)
{
	if (!s_dev.loaded)
	{
		const char *dir = getenv("CTR_DEV_SHOT_DIR");
		s_dev.loaded = 1;
		s_dev.demoTrack = NativeDev_EnvInt("CTR_DEV_DEMO", -1);
		s_dev.fast = NativeDev_EnvInt("CTR_DEV_FAST", 0);
		s_dev.hidden = NativeDev_EnvInt("CTR_DEV_HIDDEN", 0);
		s_dev.noItems = NativeDev_EnvInt("CTR_DEV_NO_ITEMS", 0);
		s_dev.botSpeed = NativeDev_EnvInt("CTR_DEV_BOT_SPEED", 100);
		s_dev.shotDir = (dir != NULL && *dir != '\0') ? dir : NULL;
		s_dev.shotEvery = NativeDev_EnvInt("CTR_DEV_SHOT_EVERY", 60);
		if (s_dev.shotEvery < 1)
			s_dev.shotEvery = 1;
		s_dev.shotFrom = NativeDev_EnvInt("CTR_DEV_SHOT_FROM", 0);
		s_dev.exitFrame = NativeDev_EnvInt("CTR_DEV_EXIT_FRAME", 0);
		s_dev.dumpFrame = NativeDev_EnvInt("CTR_DEV_DUMP_FRAME", -1);
	}
	return &s_dev;
}

#define NATIVE_DEV_MAX_TAGS 64

static struct
{
	const char *start;
	const char *end;
	int tag;
} s_devTags[NATIVE_DEV_MAX_TAGS];
static int s_devTagCount;

void NativeDev_TagPrims(const void *start, const void *end, int tag)
{
	if (s_devTagCount < NATIVE_DEV_MAX_TAGS && (const char *)end > (const char *)start)
	{
		s_devTags[s_devTagCount].start = (const char *)start;
		s_devTags[s_devTagCount].end = (const char *)end;
		s_devTags[s_devTagCount].tag = tag;
		s_devTagCount++;
	}
}

int NativeDev_PrimTag(const void *addr)
{
	const char *p = (const char *)addr;
	for (int i = 0; i < s_devTagCount; i++)
	{
		if (p >= s_devTags[i].start && p < s_devTags[i].end)
			return s_devTags[i].tag;
	}
	return -1;
}

FILE *NativeDev_DumpFile(void)
{
	NativeDevState *dev = NativeDev_State();
	if (dev->frame != dev->dumpFrame)
		return NULL;
	if (dev->dump == NULL)
	{
		char path[1024];
		snprintf(path, sizeof(path), "%s/polys-%06d.txt", dev->shotDir ? dev->shotDir : ".", dev->frame);
		dev->dump = fopen(path, "w");
	}
	return dev->dump;
}

int NativeDev_DemoTrack(void) { return NativeDev_State()->demoTrack; }
int NativeDev_Fast(void) { return NativeDev_State()->fast; }
int NativeDev_Hidden(void) { return NativeDev_State()->hidden; }
int NativeDev_NoItems(void) { return NativeDev_State()->noItems; }
int NativeDev_BotSpeedPercent(void) { return NativeDev_State()->botSpeed; }

// 24-bit top-down BMP, readable by every image tool with no extra library.
static void NativeDev_WriteBmp(const char *path, const unsigned char *rgba, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (f == NULL)
		return;
	const int rowBytes = (w * 3 + 3) & ~3;
	const unsigned int dataSize = (unsigned int)rowBytes * (unsigned int)h;
	unsigned char header[54] = {'B', 'M'};
	const unsigned int fileSize = 54 + dataSize;
	const int negH = -h;
	memcpy(header + 2, &fileSize, 4);
	header[10] = 54;
	header[14] = 40;
	memcpy(header + 18, &w, 4);
	memcpy(header + 22, &negH, 4);
	header[26] = 1;
	header[28] = 24;
	memcpy(header + 34, &dataSize, 4);
	fwrite(header, 1, sizeof(header), f);
	unsigned char *row = (unsigned char *)calloc((size_t)rowBytes, 1);
	for (int y = 0; y < h && row != NULL; y++)
	{
		// GL rows are bottom-up; the BMP is top-down.
		const unsigned char *src = rgba + (size_t)(h - 1 - y) * (size_t)w * 4;
		for (int x = 0; x < w; x++)
		{
			row[x * 3 + 0] = src[x * 4 + 2];
			row[x * 3 + 1] = src[x * 4 + 1];
			row[x * 3 + 2] = src[x * 4 + 0];
		}
		fwrite(row, 1, (size_t)rowBytes, f);
	}
	free(row);
	fclose(f);
}

// The VRAM rectangle as bottom-up RGBA, like NativeRenderer_ReadMainTarget.
static unsigned char *NativeDev_ReadVRAMRect(NativeDevState *dev, int *width, int *height)
{
	const int w = dev->vramW, h = dev->vramH;
	unsigned short *px = (unsigned short *)malloc((size_t)w * (size_t)h * 2);
	unsigned char *rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
	if (px == NULL || rgba == NULL)
	{
		free(px);
		free(rgba);
		return NULL;
	}
	NativeRenderer_ReadVRAM(px, dev->vramX, dev->vramY, w, h);
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			const unsigned short p = px[y * w + x];
			unsigned char *o = &rgba[((size_t)(h - 1 - y) * w + x) * 4];
			o[0] = (unsigned char)((p & 31) << 3);
			o[1] = (unsigned char)(((p >> 5) & 31) << 3);
			o[2] = (unsigned char)(((p >> 10) & 31) << 3);
			o[3] = 255;
		}
	}
	free(px);
	*width = w;
	*height = h;
	return rgba;
}

void NativeDev_AudioFrames(const short *frames, int frameCount)
{
	static FILE *out;
	static int opened;
	if (!opened)
	{
		const char *path = getenv("CTR_DEV_AUDIO");
		opened = 1;
		if ((path != NULL) && (*path != '\0'))
			out = fopen(path, "wb");
	}
	if ((out != NULL) && (frameCount > 0))
	{
		fwrite(frames, sizeof(short) * 2, (size_t)frameCount, out);
		fflush(out);
	}
}

void NativeDev_EndFrameFromVRAM(int x, int y, int w, int h)
{
	NativeDevState *dev = NativeDev_State();
	dev->vramShot = 1;
	dev->vramX = x;
	dev->vramY = y;
	dev->vramW = w;
	dev->vramH = h;
	NativeDev_EndFrame();
	dev->vramShot = 0;
}

void NativeDev_EndFrame(void)
{
	NativeDevState *dev = NativeDev_State();
	if (dev->dump != NULL)
	{
		fclose(dev->dump);
		dev->dump = NULL;
	}
	const int frame = dev->frame++;
	s_devTagCount = 0;
	NativeDev_RaceFrame();

	if (dev->shotDir != NULL && frame >= dev->shotFrom && ((frame - dev->shotFrom) % dev->shotEvery) == 0)
	{
		int w, h;
		unsigned char *rgba = dev->vramShot ? NativeDev_ReadVRAMRect(dev, &w, &h) : NativeRenderer_ReadMainTarget(&w, &h);
		if (rgba != NULL)
		{
			char path[1024];
			snprintf(path, sizeof(path), "%s/f%06d.bmp", dev->shotDir, frame);
			NativeDev_WriteBmp(path, rgba, w, h);
			free(rgba);
		}
	}

	if (dev->exitFrame > 0 && frame + 1 >= dev->exitFrame)
	{
		NativeDev_RaceShutdown();
		exit(0);
	}
}
