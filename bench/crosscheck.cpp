// Cross-validate the encoder output against both independent BC7 decoders,
// emphasizing mode-0 blocks. Generates random multi-region hard-edge blocks.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#define color_rgba enc_color_rgba
#include "bc7enc.h"
#undef color_rgba
#include "bc7decomp.h"
#include "bc7decomp_ref.cpp"

static unsigned block_mode(const uint8_t* blk)
{
	unsigned m = 0;
	while (m < 8 && ((blk[0] & (1 << m)) == 0)) m++;
	return m;
}

int main()
{
	bc7enc_compress_block_init();
	bc7enc_compress_block_params p;
	bc7enc_compress_block_params_init(&p);
	bc7enc_compress_block_params_init_linear_weights(&p);

	srand(12345);
	unsigned mismatches = 0, mode0_blocks = 0, total = 20000;
	for (unsigned t = 0; t < total; t++)
	{
		// Random 2-4 flat color regions split along rows/columns/quadrants.
		enc_color_rgba px[16];
		unsigned regions = 2 + (rand() % 3);
		enc_color_rgba palette[4];
		for (unsigned k = 0; k < regions; k++)
			palette[k] = { { (uint8_t)(rand() & 255), (uint8_t)(rand() & 255), (uint8_t)(rand() & 255), 255 } };
		for (unsigned yy = 0; yy < 4; yy++)
			for (unsigned xx = 0; xx < 4; xx++)
			{
				unsigned k;
				if (regions == 2) k = (xx < 2) ? 0 : 1;
				else if (regions == 3) k = (xx < 1) ? 0 : (xx < 3 ? 1 : 2);
				else k = (yy < 2 ? 0 : 2) + (xx >= 2 ? 1 : 0);
				px[yy * 4 + xx] = palette[k];
			}

		uint8_t blk[16];
		bc7enc_compress_block(blk, px, &p);
		if (block_mode(blk) == 0) mode0_blocks++;

		bc7decomp::color_rgba d1[16], d2[16];
		bc7decomp::unpack_bc7(blk, d1);
		bc7decomp_ref::unpack_bc7(blk, d2);
		for (unsigned i = 0; i < 16; i++)
			for (unsigned c = 0; c < 4; c++)
				if (d1[i].m_comps[c] != d2[i].m_comps[c])
				{
					mismatches++;
					break;
				}
	}

	printf("blocks=%u mode0=%u decoder_mismatches=%u\n", total, mode0_blocks, mismatches);
	return mismatches ? 1 : 0;
}
