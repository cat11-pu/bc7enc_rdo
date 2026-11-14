// Generate realistic test PNG textures: photo-like gradients, UI/text-like edges,
// checkerboard, and a sprite with alpha. Run once to populate bench/png/.
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <string>
#include "lodepng.h"

static void save(const char* path, unsigned w, unsigned h, const std::vector<uint8_t>& rgba)
{
	std::vector<uint8_t> out;
	lodepng::encode(out, rgba, w, h);
	lodepng::save_file(out, path);
	printf("wrote %s\n", path);
}

int main()
{
	const unsigned S = 256;

	// 1. Photo-like: smooth radial shading + soft color variation, low frequency.
	{
		std::vector<uint8_t> img(S * S * 4);
		for (unsigned y = 0; y < S; y++)
			for (unsigned x = 0; x < S; x++)
			{
				float dx = (x - S / 2.0f) / (S / 2.0f), dy = (y - S / 2.0f) / (S / 2.0f);
				float r = sqrtf(dx * dx + dy * dy);
				uint8_t* p = &img[(y * S + x) * 4];
				p[0] = (uint8_t)fminf(255, 120 + 90 * (1 - r) + 20 * sinf(x * .05f));
				p[1] = (uint8_t)fminf(255, 80 + 120 * (1 - r * .7f) + 15 * cosf(y * .06f));
				p[2] = (uint8_t)fminf(255, 160 + 70 * r);
				p[3] = 255;
			}
		save("bench/png/photo.png", S, S, img);
	}

	// 2. UI/text-like: high-contrast rectangular glyph edges on a flat background.
	{
		std::vector<uint8_t> img(S * S * 4, 235);
		for (unsigned i = 0; i < S * S; i++) img[i * 4 + 3] = 255;
		for (unsigned gy = 0; gy < 8; gy++)
			for (unsigned gx = 0; gx < 8; gx++)
			{
				unsigned ox = gx * 32 + 4, oy = gy * 32 + 4;
				// simple glyph: vertical bar + horizontal bar + dot, 1-2 px features
				for (unsigned yy = 0; yy < 24; yy++)
					for (unsigned xx = 0; xx < 6; xx++)
					{
						unsigned x = ox + xx, y = oy + yy;
						uint8_t* p = &img[(y * S + x) * 4];
						p[0] = 20; p[1] = 24; p[2] = 30;
					}
				for (unsigned xx = 0; xx < 20; xx++)
					for (unsigned yy = 0; yy < 5; yy++)
					{
						unsigned x = ox + xx, y = oy + 8 + yy;
						uint8_t* p = &img[(y * S + x) * 4];
						p[0] = 20; p[1] = 24; p[2] = 30;
					}
			}
		save("bench/png/ui_text.png", S, S, img);
	}

	// 3. Checkerboard at multiple frequencies (hard edges, several colors per 4x4 block).
	{
		std::vector<uint8_t> img(S * S * 4);
		for (unsigned y = 0; y < S; y++)
			for (unsigned x = 0; x < S; x++)
			{
				uint8_t* p = &img[(y * S + x) * 4];
				bool c = ((x >> 2) ^ (y >> 2)) & 1;
				p[0] = c ? 230 : 25;
				p[1] = c ? 225 : 30;
				p[2] = c ? 40 : 210;
				p[3] = 255;
			}
		save("bench/png/checker.png", S, S, img);
	}

	// 4. Sprite with alpha: hard color + alpha cutout edges.
	{
		std::vector<uint8_t> img(S * S * 4, 0);
		for (unsigned y = 0; y < S; y++)
			for (unsigned x = 0; x < S; x++)
			{
				float dx = (x - S / 2.0f) / (S / 3.0f), dy = (y - S / 2.0f) / (S / 3.0f);
				float d = sqrtf(dx * dx + dy * dy);
				uint8_t* p = &img[(y * S + x) * 4];
				if (d < 0.9f)
				{
					p[0] = (uint8_t)(200 + 40 * cosf(x * .1f));
					p[1] = (uint8_t)(60 + 30 * sinf(y * .1f));
					p[2] = 90;
					p[3] = d > 0.82f ? (uint8_t)(255 * (0.9f - d) / 0.08f) : 255;
				}
			}
		save("bench/png/sprite_alpha.png", S, S, img);
	}

	return 0;
}
