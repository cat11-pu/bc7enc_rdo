// Exhaustive mode-1 partition scan using the public block API through a small
// instrumented encoder variant is not available, so replicate mode-1 encoding
// by forcing partitions via a temporary copy of the partition tables.
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include "bc7enc.h"
#include "bc7decomp.h"

static unsigned block_mode(const uint8_t* blk)
{
	unsigned m = 0;
	while (m < 8 && ((blk[0] & (1 << m)) == 0)) m++;
	return m;
}

static uint64_t block_sse(const color_rgba px[16], const uint8_t blk[16])
{
	bc7decomp::color_rgba dec[16];
	bc7decomp::unpack_bc7(blk, dec);
	uint64_t err = 0;
	for (unsigned i = 0; i < 16; i++)
		for (unsigned c = 0; c < 3; c++)
		{
			int64_t e = (int64_t)dec[i].m_comps[c] - px[i].m_c[c];
			err += (uint64_t)(e * e);
		}
	return err;
}

int main()
{
	bc7enc_compress_block_init();
	static const uint8_t dump[16][4] = {
		{35,210,20,255}, {35,210,20,255}, {220,210,20,255}, {220,210,20,255},
		{35,210,20,255}, {35,210,20,255}, {220,210,20,255}, {220,210,200,255},
		{35,45,20,255},  {35,45,20,255},  {220,45,200,255}, {220,45,200,255},
		{35,45,20,255},  {35,45,200,255}, {220,45,200,255}, {220,45,200,255},
	};
	color_rgba px[16];
	for (unsigned i = 0; i < 16; i++)
		px[i] = { { dump[i][0], dump[i][1], dump[i][2], dump[i][3] } };

	bc7enc_compress_block_params base;
	bc7enc_compress_block_params_init(&base);
	bc7enc_compress_block_params_init_linear_weights(&base);

	// Baseline full encoder choice.
	{
		uint8_t blk[16];
		bc7enc_compress_block_params p = base;
		bc7enc_compress_block(blk, px, &p);
		printf("default: mode%u sse=%llu\n\n", block_mode(blk), (unsigned long long)block_sse(px, blk));
	}

	// Reference: how well does a 2-subset ideal assignment do?
	// Print unique colors and counts.
	struct Col { uint8_t c[3]; unsigned n; };
	std::vector<Col> uniq;
	for (unsigned i = 0; i < 16; i++)
	{
		bool found = false;
		for (auto& u : uniq)
			if (u.c[0] == px[i].m_c[0] && u.c[1] == px[i].m_c[1] && u.c[2] == px[i].m_c[2]) { u.n++; found = true; break; }
		if (!found) uniq.push_back({ {px[i].m_c[0], px[i].m_c[1], px[i].m_c[2]}, 1 });
	}
	printf("unique colors: %u\n", (unsigned)uniq.size());
	for (auto& u : uniq) printf("  (%u,%u,%u) x%u\n", u.c[0], u.c[1], u.c[2], u.n);

	// Best possible mode-1 error with each partition, evaluated by a brute force
	// subset mean quantization done here approximately via the encoder by faking
	// single-subset images is impractical; instead show per-pixel distance from
	// each unique color to illustrate why 2 subsets cannot separate 5 colors.
	printf("\nPairwise color distances (RGB):\n");
	for (unsigned i = 0; i < uniq.size(); i++)
		for (unsigned j = i + 1; j < uniq.size(); j++)
		{
			unsigned d = 0;
			for (unsigned c = 0; c < 3; c++) d += (unsigned)(abs((int)uniq[i].c[c] - uniq[j].c[c]) * abs((int)uniq[i].c[c] - uniq[j].c[c]));
			printf("  c%u-c%u: %u\n", i, j, d);
		}
	return 0;
}
