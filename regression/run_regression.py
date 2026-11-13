#!/usr/bin/env python3
"""bc7enc regression harness.

For every PNG in the corpus:
  1. Encode twice (run A / run B) with the same encoder settings.
  2. Verify the two DDS outputs are byte-identical (whole file + per 16-byte block).
  3. Parse RGB/RGBA PSNR from encoder output and require PSNR >= baseline.

Baseline (baseline.json) stores per-texture PSNR and DDS sha256. First run
with --update-baseline creates it; later runs verify against it.
"""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

PSNR_RGB_RE = re.compile(r"^RGB\s+Max error:.*?PSNR ([0-9.]+) dB", re.M)
PSNR_RGBA_RE = re.compile(r"^RGBA\s+Max error:.*?PSNR ([0-9.]+) dB", re.M)
PSNR_EPSILON = 0.01  # dB tolerance for float print parsing
BLOCK_SIZE = 16      # BC7 block size in bytes


def encode_once(encoder, extra_args, src, dds_out, decoded_out, env):
    cmd = [encoder] + extra_args + [src, dds_out, decoded_out]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=600)
    if proc.returncode != 0:
        raise RuntimeError("encoder failed rc=%d: %s" % (proc.returncode, proc.stderr[-500:]))
    out = proc.stdout
    m_rgb = PSNR_RGB_RE.search(out)
    m_rgba = PSNR_RGBA_RE.search(out)
    if not m_rgb:
        raise RuntimeError("no PSNR in encoder output")
    return float(m_rgb.group(1)), (float(m_rgba.group(1)) if m_rgba else None)


def block_diffs(data_a, data_b, payload_off):
    """Return list of differing 16-byte block indices within the BC7 payload."""
    diffs = []
    n = (len(data_a) - payload_off) // BLOCK_SIZE
    for i in range(n):
        off = payload_off + i * BLOCK_SIZE
        if data_a[off:off + BLOCK_SIZE] != data_b[off:off + BLOCK_SIZE]:
            diffs.append(i)
    return diffs


def dds_payload_offset(dds_len, width, height):
    payload = (width // 4) * (height // 4) * BLOCK_SIZE
    return dds_len - payload


def process_image(args, png_path, work_root):
    name = os.path.basename(png_path)
    stem = os.path.splitext(name)[0]
    work = os.path.join(work_root, stem)
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, OMP_NUM_THREADS=str(args.omp_threads))
    result = {"name": name, "ok": True, "errors": []}
    try:
        runs = []
        for tag in ("a", "b"):
            dds = os.path.join(work, stem + "_%s.dds" % tag)
            dec = os.path.join(work, stem + "_%s_dec.png" % tag)
            psnr = encode_once(args.encoder, args.encoder_args.split(),
                               png_path, dds, dec, env)
            with open(dds, "rb") as f:
                blob = f.read()
            runs.append((psnr, blob, dds, dec))
        (psnr_a, blob_a, dds_a, dec_a), (psnr_b, blob_b, _, _) = runs
        result["psnr_rgb"], result["psnr_rgba"] = psnr_a
        result["psnr_rgb_b"] = psnr_b[0]
        result["sha256"] = hashlib.sha256(blob_a).hexdigest()
        result["size"] = len(blob_a)

        # Byte-identical check (whole file)
        if blob_a != blob_b:
            result["ok"] = False
            result["errors"].append("run A/B DDS bytes differ")
        # Per-block determinism check
        # image dims come from the manifest entry passed via args
        w, h = args.dims[name]
        off = dds_payload_offset(len(blob_a), w, h)
        if len(blob_a) == len(blob_b):
            diffs = block_diffs(blob_a, blob_b, off)
            result["block_diffs"] = diffs[:8]
            result["num_blocks"] = (len(blob_a) - off) // BLOCK_SIZE
            if diffs:
                result["ok"] = False
                result["errors"].append("%d/%d blocks differ between runs (first: %s)"
                                        % (len(diffs), result["num_blocks"], diffs[:8]))
        # PSNR stability between runs
        if abs(psnr_a[0] - psnr_b[0]) > PSNR_EPSILON:
            result["ok"] = False
            result["errors"].append("PSNR differs between runs: %.3f vs %.3f"
                                    % (psnr_a[0], psnr_b[0]))
        # keep one decoded png + dds for worst-sample extraction
        result["dds_path"], result["dec_path"] = dds_a, dec_a
    except Exception as e:
        result["ok"] = False
        result["errors"].append(str(e))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--encoder", default="./bc7enc")
    ap.add_argument("--encoder-args", default="", help="extra encoder flags, e.g. '-e' or '-z1.0'")
    ap.add_argument("--corpus", default="regression/corpus")
    ap.add_argument("--baseline", default="regression/baseline.json")
    ap.add_argument("--report", default="regression/regression_report.md")
    ap.add_argument("--worst-dir", default="regression/worst_samples")
    ap.add_argument("--worst-n", type=int, default=10)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--omp-threads", type=int, default=2)
    ap.add_argument("--update-baseline", action="store_true")
    args = ap.parse_args()

    # manifest: dims per file (regenerate cheaply via gen_textures naming)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gen_textures as gt
    dims = {}
    cats = {}
    import zlib as _zlib, random as _random
    for cat in gt.CATEGORIES:
        for i in range(gt.COUNT_PER_CATEGORY[cat]):
            w, h = gt.SIZES[(i + gt.CATEGORIES.index(cat)) % len(gt.SIZES)]
            name = "%s_%03d.png" % (cat, i)
            dims[name] = (w, h)
            cats[name] = cat
    args.dims = dims

    pngs = sorted(os.path.join(args.corpus, f) for f in os.listdir(args.corpus)
                  if f.endswith(".png"))
    print("regression: %d textures, encoder=%s args='%s'" %
          (len(pngs), args.encoder, args.encoder_args))

    with tempfile.TemporaryDirectory(dir=os.path.dirname(os.path.abspath(args.report))) as work_root:
        results = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = {ex.submit(process_image, args, p, work_root): p for p in pngs}
            done = 0
            for fut in concurrent.futures.as_completed(futs):
                results.append(fut.result())
                done += 1
                if done % 50 == 0:
                    print("  %d/%d done" % (done, len(pngs)), flush=True)

        results.sort(key=lambda r: r["name"])

        baseline = {}
        if os.path.exists(args.baseline) and not args.update_baseline:
            with open(args.baseline) as f:
                baseline = json.load(f)

        # PSNR vs baseline
        regressions = []
        hash_drift = []
        for r in results:
            if not r.get("ok"):
                continue
            b = baseline.get(r["name"])
            if b:
                if r["psnr_rgb"] < b["psnr_rgb"] - PSNR_EPSILON:
                    r["ok"] = False
                    r["errors"].append("RGB PSNR %.3f < baseline %.3f"
                                       % (r["psnr_rgb"], b["psnr_rgb"]))
                    regressions.append(r["name"])
                if r.get("psnr_rgba") is not None and b.get("psnr_rgba") is not None:
                    if r["psnr_rgba"] < b["psnr_rgba"] - PSNR_EPSILON:
                        r["ok"] = False
                        r["errors"].append("RGBA PSNR %.3f < baseline %.3f"
                                           % (r["psnr_rgba"], b["psnr_rgba"]))
                        regressions.append(r["name"])
                if r["sha256"] != b.get("sha256"):
                    hash_drift.append(r["name"])

        if args.update_baseline or not baseline:
            new_base = {r["name"]: {"psnr_rgb": r["psnr_rgb"],
                                    "psnr_rgba": r.get("psnr_rgba"),
                                    "sha256": r["sha256"]}
                        for r in results if "psnr_rgb" in r}
            os.makedirs(os.path.dirname(args.baseline), exist_ok=True)
            with open(args.baseline, "w") as f:
                json.dump(new_base, f, indent=1, sort_keys=True)
            print("baseline written: %s (%d entries)" % (args.baseline, len(new_base)))

        # stats
        passed = [r for r in results if r["ok"]]
        failed = [r for r in results if not r["ok"]]
        det_fail = [r for r in results if any("block" in e or "bytes differ" in e
                                              for e in r["errors"])]
        with_psnr = [r for r in results if "psnr_rgb" in r]
        worst = sorted(with_psnr, key=lambda r: r["psnr_rgb"])[:args.worst_n]

        # export worst samples
        os.makedirs(args.worst_dir, exist_ok=True)
        for old in os.listdir(args.worst_dir):
            os.unlink(os.path.join(args.worst_dir, old))
        for r in worst:
            stem = os.path.splitext(r["name"])[0]
            shutil.copy(os.path.join(args.corpus, r["name"]),
                        os.path.join(args.worst_dir, r["name"]))
            if os.path.exists(r["dec_path"]):
                shutil.copy(r["dec_path"], os.path.join(args.worst_dir, stem + "_decoded.png"))
            if os.path.exists(r["dds_path"]):
                shutil.copy(r["dds_path"], os.path.join(args.worst_dir, stem + ".dds"))

        # per-category stats
        cat_stats = {}
        for r in with_psnr:
            c = cats[r["name"]]
            s = cat_stats.setdefault(c, {"n": 0, "min": 1e9, "sum": 0.0, "max": 0.0})
            s["n"] += 1
            s["min"] = min(s["min"], r["psnr_rgb"])
            s["max"] = max(s["max"], r["psnr_rgb"])
            s["sum"] += r["psnr_rgb"]

        overall_pass = not failed
        lines = []
        lines.append("# BC7 编码回归报告")
        lines.append("")
        lines.append("- 日期: %s" % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        lines.append("- 编码器: `%s %s`" % (args.encoder, args.encoder_args))
        lines.append("- 语料: %d 张贴图 (solid/gradient/hardedge/noise/transparent/normalmap)"
                     % len(results))
        lines.append("- 总体结论: **%s**" % ("PASS" if overall_pass else "FAIL"))
        lines.append("")
        lines.append("## 验收标准")
        lines.append("")
        lines.append("| 检查项 | 结果 |")
        lines.append("|---|---|")
        lines.append("| 每张 PSNR 不低于基线 | %s (%d 张回退) |"
                     % ("PASS" if not regressions else "FAIL", len(regressions)))
        lines.append("| 两次运行字节一致 | %s (%d 张失败) |"
                     % ("PASS" if not det_fail else "FAIL", len(det_fail)))
        lines.append("| 逐块 (16 字节) 确定 | %s |"
                     % ("PASS" if not det_fail else "FAIL"))
        lines.append("| 输出哈希相对基线漂移 | %d 张 (提示性) |" % len(hash_drift))
        lines.append("")
        lines.append("## 分类 PSNR 统计 (RGB, dB)")
        lines.append("")
        lines.append("| 类别 | 数量 | 最低 | 平均 | 最高 |")
        lines.append("|---|---|---|---|---|")
        for c in gt.CATEGORIES:
            s = cat_stats.get(c)
            if s:
                lines.append("| %s | %d | %.3f | %.3f | %.3f |"
                             % (c, s["n"], s["min"], s["sum"] / s["n"], s["max"]))
        if with_psnr:
            allp = [r["psnr_rgb"] for r in with_psnr]
            lines.append("| **总计** | %d | %.3f | %.3f | %.3f |"
                         % (len(allp), min(allp), sum(allp) / len(allp), max(allp)))
        lines.append("")
        lines.append("## 最差样本 (按 RGB PSNR 升序, 前 %d)" % args.worst_n)
        lines.append("")
        lines.append("| 排名 | 样本 | 类别 | RGB PSNR | RGBA PSNR | 文件 |")
        lines.append("|---|---|---|---|---|---|")
        for i, r in enumerate(worst):
            rgba = "%.3f" % r["psnr_rgba"] if r.get("psnr_rgba") is not None else "-"
            lines.append("| %d | %s | %s | %.3f | %s | worst_samples/%s |"
                         % (i + 1, r["name"], cats[r["name"]], r["psnr_rgb"], rgba, r["name"]))
        lines.append("")
        if failed:
            lines.append("## 失败明细")
            lines.append("")
            for r in failed:
                lines.append("- `%s`: %s" % (r["name"], "; ".join(r["errors"])))
            lines.append("")
        if hash_drift:
            lines.append("## 哈希漂移 (PSNR 达标但字节与基线不同)")
            lines.append("")
            for n in hash_drift[:20]:
                lines.append("- `%s`" % n)
            lines.append("")

        os.makedirs(os.path.dirname(args.report), exist_ok=True)
        with open(args.report, "w") as f:
            f.write("\n".join(lines) + "\n")
        print("report: %s" % args.report)
        print("worst samples: %s" % args.worst_dir)
        print("RESULT: %s (%d/%d passed)" % ("PASS" if overall_pass else "FAIL",
                                             len(passed), len(results)))
        return 0 if overall_pass else 1


if __name__ == "__main__":
    sys.exit(main())
