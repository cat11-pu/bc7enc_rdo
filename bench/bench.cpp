// Benchmark harness for bc7enc block encoder improvements.
// Generates hard-edge and gradient textures, encodes with bc7enc.cpp,
// decodes with the reference BC7 decoder and reports PSNR + timing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <chrono>

#include "bc7enc.h"
#include "bc7decomp.h"
#include "lodepng.h"

struct Image
{
	unsigned w = 0, h = 0;
	std::vector<uint8_t> rgba;
};

static void put_pixel(Image& img, unsigned x, unsigned y, int r, int g, int b, int a = 255)
{
	uint8_t* p = &img.rgba[(y * img.w + x) * 4];
	p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

static Image make_edge_image(unsigned size)
{
	Image img;
	img.w = img.h = size;
	img.rgba.resize((size_t)size * size * 4);
	for (unsigned y = 0; y < size; y++)
		for (unsigned x = 0; x < size; x++)
		{
			// Vertical hard edge with a 1-pixel anti-aliased transition, plus a diagonal edge.
			int r = (x < size / 2) ? 40 : 215;
			int g = (y < size / 2) ? 60 : 190;
			int b = ((x + y) < size) ? 200 : 30;
			if (x == size / 2) { r = 128; g = 128; b = 128; }
			put_pixel(img, x, y, r, g, b);
		}
	return img;
}

static Image make_gradient_image(unsigned size)
{
	Image img;
	img.w = img.h = size;
	img.rgba.resize((size_t)size * size * 4);
	for (unsigned y = 0; y < size; y++)
		for (unsigned x = 0; x < size; x++)
		{
			// Smooth multi-channel ramp: stresses endpoint quantization (banding).
			int r = (int)(255.0f * x / (size - 1));
			int g = (int)(255.0f * y / (size - 1));
			int b = (int)(255.0f * (0.5f + 0.5f * sinf((x + y) * 6.2831853f / size)));
			put_pixel(img, x, y, r, g, b);
		}
	return img;
}

static Image make_mixed_image(unsigned size)
{
	Image img;
	img.w = img.h = size;
	img.rgba.resize((size_t)size * size * 4);
	for (unsigned by = 0; by < size / 4; by++)
		for (unsigned bx = 0; bx < size / 4; bx++)
		{
			bool edge_block = ((bx * 7 + by * 13) & 3) < 2;
			for (unsigned yy = 0; yy < 4; yy++)
				for (unsigned xx = 0; xx < 4; xx++)
				{
					unsigned x = bx * 4 + xx, y = by * 4 + yy;
					if (edge_block)
					{
						int r = (xx < 2) ? 35 : 220;
						int g = (yy < 2) ? 210 : 45;
						int b = (xx + yy < 4) ? 20 : 200;
						put_pixel(img, x, y, r, g, b);
					}
					else
					{
						int r = (int)(255.0f * x / (size - 1));
						int g = (int)(255.0f * y / (size - 1));
						int b = (int)(128.0f + 100.0f * sinf(x * 0.25f) * cosf(y * 0.21f));
						put_pixel(img, x, y, r, g, b);
					}
				}
		}
	return img;
}

static Image make_alpha_image(unsigned size)
{
	Image img;
	img.w = img.h = size;
	img.rgba.resize((size_t)size * size * 4);
	for (unsigned y = 0; y < size; y++)
		for (unsigned x = 0; x < size; x++)
		{
			int r = (int)(255.0f * x / (size - 1));
			int g = 90;
			int b = 160;
			int a = (x < size / 2) ? 40 : 230;
			if (x == size / 2) a = 128;
			put_pixel(img, x, y, r, g, b, a);
		}
	return img;
}

static bool load_png(const char* path, Image& out)
{
	std::vector<uint8_t> file;
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	file.resize(n);
	if (fread(file.data(), 1, n, f) != (size_t)n) { fclose(f); return false; }
	fclose(f);

	std::vector<uint8_t> decoded;
	unsigned w, h;
	unsigned err = lodepng::decode(decoded, w, h, file, LCT_RGBA, 8);
	if (err) return false;
	out.w = w; out.h = h; out.rgba = std::move(decoded);
	return true;
}

struct Metrics
{
	double mse_rgb = 0;
	double mse_rgba = 0;
	uint64_t max_err = 0;
	unsigned mode_count[8] = { 0,0,0,0,0,0,0,0 };
	unsigned blocks = 0;
};

static unsigned block_mode(const uint8_t* blk)
{
	unsigned m = 0;
	while (m < 8 && ((blk[0] & (1 << m)) == 0)) m++;
	return m;
}

static void encode_image(const Image& img, const bc7enc_compress_block_params& params,
	std::vector<uint8_t>& blocks, Metrics& met, int repeat)
{
	const unsigned bw = img.w / 4, bh = img.h / 4;
	blocks.resize((size_t)bw * bh * 16);

	auto run = [&]() {
		for (unsigned by = 0; by < bh; by++)
			for (unsigned bx = 0; bx < bw; bx++)
			{
				color_rgba px[16];
				for (unsigned yy = 0; yy < 4; yy++)
					for (unsigned xx = 0; xx < 4; xx++)
					{
						const uint8_t* s = &img.rgba[((by * 4 + yy) * img.w + (bx * 4 + xx)) * 4];
						px[yy * 4 + xx] = { { s[0], s[1], s[2], s[3] } };
					}
				bc7enc_compress_block(&blocks[((size_t)by * bw + bx) * 16], px, &params);
			}
	};

	run();
	auto t0 = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < repeat; i++)
		run();
	auto t1 = std::chrono::high_resolution_clock::now();
	double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / repeat;

	met = Metrics();
	met.blocks = bw * bh;
	double sum_rgb = 0, sum_rgba = 0;
	for (unsigned by = 0; by < bh; by++)
		for (unsigned bx = 0; bx < bw; bx++)
		{
			const uint8_t* blk = &blocks[((size_t)by * bw + bx) * 16];
			met.mode_count[block_mode(blk)]++;
			bc7decomp::color_rgba dec[16];
			bc7decomp::unpack_bc7(blk, dec);
			for (unsigned yy = 0; yy < 4; yy++)
				for (unsigned xx = 0; xx < 4; xx++)
				{
					const uint8_t* s = &img.rgba[((by * 4 + yy) * img.w + (bx * 4 + xx)) * 4];
					const bc7decomp::color_rgba& d = dec[yy * 4 + xx];
					for (unsigned c = 0; c < 3; c++)
					{
						double e = (double)d.m_comps[c] - s[c];
						sum_rgb += e * e;
						sum_rgba += e * e;
						uint64_t ae = (uint64_t)fabs(e);
						if (ae > met.max_err) met.max_err = ae;
					}
					double ea = (double)d.m_comps[3] - s[3];
					sum_rgba += ea * ea;
					uint64_t aea = (uint64_t)fabs(ea);
					if (aea > met.max_err) met.max_err = aea;
				}
		}

	size_t npix_rgb = (size_t)bw * bh * 16 * 3;
	size_t npix_rgba = (size_t)bw * bh * 16 * 4;
	met.mse_rgb = sum_rgb / npix_rgb;
	met.mse_rgba = sum_rgba / npix_rgba;

	printf("  time=%.2f ms  PSNR_RGB=%.3f dB  PSNR_RGBA=%.3f dB  maxErr=%llu  modes[",
		ms, 10.0 * log10(255.0 * 255.0 / met.mse_rgb),
		10.0 * log10(255.0 * 255.0 / met.mse_rgba),
		(unsigned long long)met.max_err);
	for (unsigned m = 0; m < 8; m++)
		if (met.mode_count[m]) printf(" m%u:%u", m, met.mode_count[m]);
	printf(" ]\n");
}

static bc7enc_compress_block_params make_params(bool perceptual)
{
	bc7enc_compress_block_params p;
	bc7enc_compress_block_params_init(&p);
	p.m_uber_level = 0;
	p.m_mode17_partition_estimation_filterbank = true;
	if (!perceptual)
		bc7enc_compress_block_params_init_linear_weights(&p);
	return p;
}

int main(int argc, char** argv)
{
	bc7enc_compress_block_init();

	std::vector<std::pair<std::string, Image>> cases;
	const unsigned SZ = 256;
	cases.push_back({ "hard_edge", make_edge_image(SZ) });
	cases.push_back({ "gradient", make_gradient_image(SZ) });
	cases.push_back({ "mixed", make_mixed_image(SZ) });
	cases.push_back({ "alpha_edge", make_alpha_image(SZ) });
	for (int i = 1; i < argc; i++)
	{
		Image img;
	if (load_png(argv[i], img))
		{
			img.w &= ~3u; img.h &= ~3u;
			cases.push_back({ argv[i], img });
		}
		else
			printf("failed loading %s\n", argv[i]);
	}

	const int repeat = 30;

	printf("=== Linear RGB metrics, default uber level 0 ===\n");
	for (auto& c : cases)
	{
		printf("[%s %ux%u]\n", c.first.c_str(), c.second.w, c.second.h);
		bc7enc_compress_block_params p = make_params(false);
		std::vector<uint8_t> blocks;
		Metrics m;
		encode_image(c.second, p, blocks, m, repeat);
	}

	printf("\n=== Perceptual (YCbCr) metrics ===\n");
	for (auto& c : cases)
	{
		printf("[%s]\n", c.first.c_str());
		bc7enc_compress_block_params p = make_params(true);
		std::vector<uint8_t> blocks;
		Metrics m;
		encode_image(c.second, p, blocks, m, repeat);
	}

	return 0;
}
