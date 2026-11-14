#include <stdio.h>
#include <stdint.h>
#include "bc7enc.h"

void dbg_exhaust_mode1(const color_rgba *pPixels, const bc7enc_compress_block_params *pComp_params);

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

	bc7enc_compress_block_params p;
	bc7enc_compress_block_params_init(&p);
	bc7enc_compress_block_params_init_linear_weights(&p);
	dbg_exhaust_mode1(px, &p);
	return 0;
}
