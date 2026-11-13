# BC7 编码回归报告

- 日期: 2026-11-14 02:21:19
- 编码器: `./bc7enc `
- 语料: 500 张贴图 (solid/gradient/hardedge/noise/transparent/normalmap)
- 总体结论: **PASS**

## 验收标准

| 检查项 | 结果 |
|---|---|
| 每张 PSNR 不低于基线 | PASS (0 张回退) |
| 两次运行字节一致 | PASS (0 张失败) |
| 逐块 (16 字节) 确定 | PASS |
| 输出哈希相对基线漂移 | 0 张 (提示性) |

## 分类 PSNR 统计 (RGB, dB)

| 类别 | 数量 | 最低 | 平均 | 最高 |
|---|---|---|---|---|
| solid | 84 | 51.933 | 87.814 | 100.000 |
| gradient | 84 | 54.896 | 57.448 | 72.374 |
| hardedge | 83 | 45.964 | 87.254 | 100.000 |
| noise | 83 | 15.414 | 44.628 | 100.000 |
| transparent | 83 | 40.164 | 58.112 | 100.000 |
| normalmap | 83 | 23.997 | 41.571 | 66.683 |
| **总计** | 500 | 15.414 | 62.844 | 100.000 |

## 最差样本 (按 RGB PSNR 升序, 前 10)

| 排名 | 样本 | 类别 | RGB PSNR | RGBA PSNR | 文件 |
|---|---|---|---|---|---|
| 1 | noise_027.png | noise | 15.414 | 16.663 | worst_samples/noise_027.png |
| 2 | noise_074.png | noise | 15.419 | 16.669 | worst_samples/noise_074.png |
| 3 | noise_077.png | noise | 15.453 | 16.702 | worst_samples/noise_077.png |
| 4 | noise_013.png | noise | 15.460 | 16.709 | worst_samples/noise_013.png |
| 5 | noise_001.png | noise | 15.467 | 16.716 | worst_samples/noise_001.png |
| 6 | noise_055.png | noise | 15.476 | 16.725 | worst_samples/noise_055.png |
| 7 | noise_019.png | noise | 15.488 | 16.738 | worst_samples/noise_019.png |
| 8 | noise_029.png | noise | 15.495 | 16.745 | worst_samples/noise_029.png |
| 9 | noise_069.png | noise | 15.503 | 16.752 | worst_samples/noise_069.png |
| 10 | noise_057.png | noise | 15.504 | 16.754 | worst_samples/noise_057.png |

