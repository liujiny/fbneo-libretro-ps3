#!/usr/bin/env python3
"""Summarize PGM logs without treating unmatched runs as an FPS benchmark."""
import argparse
import json
import pathlib
import re


def analyze(path, window_limit):
    frames, stats = [], []
    for line in path.read_text().splitlines():
        values = {key: int(value) for key, value in
                  re.findall(r"(\w+)=(\d+)", line)}
        if "avg_frame_us" in values:
            frames.append(values)
        if "shadow_pixel_bad" in values and "calls" in values:
            stats.append(values)
    checks = {
        "job_accounting": lambda s: s["submit"] - s["done"] == s["inflight"],
        "result_accounting": lambda s: s["decoded"] == s["used"] + s["abandoned"] + s["pending"],
        "shadow_accounting": lambda s: s["shadow_cmp"] + s["shadow_skipped"] == s["used"],
        "shadow_zero": lambda s: s["shadow_bad"] == s["shadow_pixel_bad"] == 0,
        "submit_origins": lambda s: s["submit"] == s["gate12_submit"] + s.get("mask_early_submit", 0) + s["tail_chain"],
    }
    selected = frames[:window_limit] if window_limit else frames
    summary = {}
    for label, windows in (("all", selected),
                           ("sprites_ge20", [f for f in selected if f["sprites_avg_frame"] >= 20])):
        count = sum(f["frames"] for f in windows)
        summary[label] = {"windows": len(windows), "frames": count}
        if count:
            for key in ("avg_frame_us", "sprite_draw_avg_us", "video_cb_avg_us",
                        "sprite_nozoom_avg_us", "sprite_zoom_avg_us",
                        "color_expand_sampled_avg_us", "color_spu_sampled_avg_us",
                        "color_ppu_sampled_avg_us", "sprites_avg_frame"):
                if all(key in f for f in windows):
                    summary[label][key] = round(sum(f[key] * f["frames"] for f in windows) / count, 3)
    last = stats[-1] if stats else {}
    ratios = {}
    for num, den in (("used", "calls"), ("poll_pending", "calls"),
                     ("stream_short_skip", "calls"), ("late_skip", "decoded"),
                     ("abandoned", "decoded"), ("chain_use", "chain_dec"),
                     ("mask_hint_calls", "calls")):
        if num in last and last.get(den):
            ratios[num + "/" + den + "_pct"] = round(last[num] * 100 / last[den], 3)
    return {"file": str(path), "total_windows": len(frames), "stats_records": len(stats),
            "violations": {name: sum(not check(s) for s in stats) for name, check in checks.items()},
            "windows_summary": summary, "last_stats": last, "ratios": ratios}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", type=pathlib.Path, nargs="+")
    parser.add_argument("--first-windows", type=int, default=0)
    args = parser.parse_args()
    for log in args.logs:
        print(json.dumps(analyze(log, args.first_windows), ensure_ascii=False, indent=2))
