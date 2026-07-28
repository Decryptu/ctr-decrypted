#include <common.h>

s32 MATH_FastSqrt(u32 value, s32 shift)
{
	u32 remainder = 0;
	s32 root = 0;
	s32 iterations = (shift >> 1) + 0xf;

	do
	{
		remainder = (remainder << 2) | (value >> 30);
		value <<= 2;

		s32 nextRoot = root * 2;
		u32 test = root * 4 + 1;

		if (test <= remainder)
		{
			remainder -= test;
			nextRoot++;
		}

		root = nextRoot;
	} while (iterations-- != 0);

	return root;
}
