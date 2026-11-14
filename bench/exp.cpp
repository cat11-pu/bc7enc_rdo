// Offline experiments on the worst edge block: what does each mode achieve?
#include <stdio.h>
#include <stdint.h>
#include <vector>
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

	// The pathological multi-color edge block dumped from diagnostics.
	color_rgba px[16];
	static const uint8_t dump[16][4] = {
		{35,210,20,255}, {35,210,20,255}, {220,210,20,255}, {220,210,20,255},
		{35,210,20,255}, {35,210,20,255}, {220,210,20,255}, {220,210,200,255},
		{35,45,20,255},  {35,45,20,255},  {220,45,200,255}, {220,45,200,255},
		{35,45,20,255},  {35,45,200,255}, {220,45,200,255}, {220,45,200,255},
	};
	for (unsigned i = 0; i < 16; i++)
		px[i] = { { dump[i][0], dump[i][1], dump[i][2], dump[i][3] } };

	bc7enc_compress_block_params base;
	bc7enc_compress_block_params_init(&base);
	bc7enc_compress_block_params_init_linear_weights(&base);

	struct Cfg { const char* name; uint32_t mask; unsigned uber; unsigned maxp; bool fb; };
	Cfg cfgs[] =
	{
		{ "default m1+m6 uber0", 0xFF, 0, 64, true },
		{ "m6 only uber0",       1u << 6, 0, 64, true },
		{ "m1 only uber0",       1u << 1, 0, 64, true },
		{ "m1+m6 uber1",         0xFF, 1, 64, true },
		{ "m1+m6 uber2",         0xFF, 2, 64, true },
		{ "m1+m6 uber4",         0xFF, 4, 64, true },
		{ "m6 only uber4",       1u << 6, 4, 64, true },
		{ "m1+m6 no filterbank", 0xFF, 0, 64, false },
	};

	for (auto& c : cfgs)
	{
		bc7enc_compress_block_params p = base;
		p.m_mode_mask = c.mask;
		p.m_uber_level = c.uber;
		p.m_max_partitions = c.maxp;
		p.m_mode17_partition_estimation_filterbank = c.fb;
		uint8_t blk[16];
		bc7enc_compress_block(blk, px, &p);
		printf("%-22s -> mode%u sse=%llu\n", c.name, block_mode(blk), (unsigned long long)block_sse(px, blk));
	}

	// Try every mode-1 partition explicitly to see the achievable error and which partition the estimator misses.
	printf("\nExhaustive mode-1 partition scan:\n");
	for (unsigned part = 0; part < 64; part++)
	{
		// Force partition via max_partitions is not possible directly; instead report estimator behavior
		// through the public API only at boundaries. (See encoder-side instrumentation.)
		(void)part;
	}
	return 0;
}
