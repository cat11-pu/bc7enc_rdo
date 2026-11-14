// Block-level diagnostics: report worst blocks and their chosen mode.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <vector>
#include <algorithm>

#include "bc7enc.h"
#include "bc7decomp.h"

struct Image { unsigned w, h; std::vector<uint8_t> rgba; };

static void put(Image& img, unsigned x, unsigned y, int r, int g, int b, int a = 255)
{
	uint8_t* p = &img.rgba[(y * img.w + x) * 4];
	p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

static Image make_mixed(unsigned size)
{
	Image img; img.w = img.h = size; img.rgba.resize((size_t)size * size * 4);
	for (unsigned by = 0; by < size / 4; by++)
		for (unsigned bx = 0; bx < size / 4; bx++)
		{
			bool edge = ((bx * 7 + by * 13) & 3) < 2;
			for (unsigned yy = 0; yy < 4; yy++)
				for (unsigned xx = 0; xx < 4; xx++)
				{
					unsigned x = bx * 4 + xx, y = by * 4 + yy;
					if (edge)
					{
						int r = (xx < 2) ? 35 : 220;
						int g = (yy < 2) ? 210 : 45;
						int b = (xx + yy < 4) ? 20 : 200;
						put(img, x, y, r, g, b);
					}
					else
					{
						int r = (int)(255.0f * x / (size - 1));
						int g = (int)(255.0f * y / (size - 1));
						int b = (int)(128.0f + 100.0f * sinf(x * .25f) * cosf(y * .21f));
						put(img, x, y, r, g, b);
					}
				}
		}
	return img;
}

static unsigned block_mode(const uint8_t* blk)
{
	unsigned m = 0;
	while (m < 8 && ((blk[0] & (1 << m)) == 0)) m++;
	return m;
}

int main()
{
	bc7enc_compress_block_init();
	const unsigned SZ = 256;
	Image img = make_mixed(SZ);
	bc7enc_compress_block_params p;
	bc7enc_compress_block_params_init(&p);
	bc7enc_compress_block_params_init_linear_weights(&p);
	p.m_uber_level = 0;

	struct Row { unsigned bx, by, mode; uint64_t err; bool edge; };
	std::vector<Row> rows;

	for (unsigned by = 0; by < SZ / 4; by++)
		for (unsigned bx = 0; bx < SZ / 4; bx++)
		{
			color_rgba px[16];
			for (unsigned yy = 0; yy < 4; yy++)
				for (unsigned xx = 0; xx < 4; xx++)
				{
					const uint8_t* s = &img.rgba[((by * 4 + yy) * img.w + (bx * 4 + xx)) * 4];
					px[yy * 4 + xx] = { { s[0], s[1], s[2], s[3] } };
				}
			uint8_t blk[16];
			bc7enc_compress_block(blk, px, &p);
			bc7decomp::color_rgba dec[16];
			bc7decomp::unpack_bc7(blk, dec);
			uint64_t err = 0;
			for (unsigned i = 0; i < 16; i++)
				for (unsigned c = 0; c < 3; c++)
				{
					int64_t e = (int64_t)dec[i].m_comps[c] - px[i].m_c[c];
					err += (uint64_t)(e * e);
				}
			bool edge = ((bx * 7 + by * 13) & 3) < 2;
			rows.push_back({ bx, by, block_mode(blk), err, edge });
		}

	std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.err > b.err; });
	printf("Top 15 worst blocks (block coords, mode, sse, type):\n");
	for (int i = 0; i < 15; i++)
		printf("  (%3u,%3u) mode%u sse=%7llu %s\n", rows[i].bx, rows[i].by, rows[i].mode,
			(unsigned long long)rows[i].err, rows[i].edge ? "EDGE" : "grad");

	uint64_t edge_err[8] = {0}, grad_err[8] = {0};
	unsigned edge_cnt[8] = {0}, grad_cnt[8] = {0};
	for (auto& r : rows)
	{
		if (r.edge) { edge_err[r.mode] += r.err; edge_cnt[r.mode]++; }
		else { grad_err[r.mode] += r.err; grad_cnt[r.mode]++; }
	}
	printf("\nBy block type and mode (count, total SSE, avg SSE):\n");
	for (unsigned m = 0; m < 8; m++)
	{
		if (edge_cnt[m]) printf("  EDGE mode%u: n=%u sse=%llu avg=%.1f\n", m, edge_cnt[m], (unsigned long long)edge_err[m], (double)edge_err[m] / edge_cnt[m]);
		if (grad_cnt[m]) printf("  GRAD mode%u: n=%u sse=%llu avg=%.1f\n", m, grad_cnt[m], (unsigned long long)grad_err[m], (double)grad_err[m] / grad_cnt[m]);
	}

	// Inspect the single worst block's pixels vs decoded.
	Row w = rows[0];
	color_rgba px[16];
	for (unsigned yy = 0; yy < 4; yy++)
		for (unsigned xx = 0; xx < 4; xx++)
		{
			const uint8_t* s = &img.rgba[((w.by * 4 + yy) * img.w + (w.bx * 4 + xx)) * 4];
			px[yy * 4 + xx] = { { s[0], s[1], s[2], s[3] } };
		}
	uint8_t blk[16];
	bc7enc_compress_block(blk, px, &p);
	bc7decomp::color_rgba dec[16];
	bc7decomp::unpack_bc7(blk, dec);
	printf("\nWorst block (%u,%u) mode%u, original vs decoded RGB:\n", w.bx, w.by, w.mode);
	printf("Raw pixel dump (row-major RGBA):\n");
	for (unsigned i = 0; i < 16; i++)
		printf("{%u,%u,%u,%u},%s", px[i].m_c[0], px[i].m_c[1], px[i].m_c[2], px[i].m_c[3], ((i & 3) == 3) ? "\n" : " ");
	printf("Re-encoding the same isolated block with identical params:\n");
	uint8_t blk2[16];
	bc7enc_compress_block(blk2, px, &p);
	printf("  isolated mode=%u sse=%llu\n", block_mode(blk2), (unsigned long long)0);
	bc7decomp::color_rgba dec2[16];
	bc7decomp::unpack_bc7(blk2, dec2);
	uint64_t iso_err = 0;
	for (unsigned i = 0; i < 16; i++)
		for (unsigned c = 0; c < 3; c++)
		{
			int64_t e = (int64_t)dec2[i].m_comps[c] - px[i].m_c[c];
			iso_err += (uint64_t)(e * e);
		}
	printf("  isolated re-encode sse=%llu\n", (unsigned long long)iso_err);
	for (unsigned yy = 0; yy < 4; yy++)
	{
		for (unsigned xx = 0; xx < 4; xx++)
		{
			unsigned i = yy * 4 + xx;
			printf("(%3u,%3u,%3u)->(%3u,%3u,%3u) ",
				px[i].m_c[0], px[i].m_c[1], px[i].m_c[2],
				dec[i].m_comps[0], dec[i].m_comps[1], dec[i].m_comps[2]);
		}
		printf("\n");
	}
	return 0;
}
