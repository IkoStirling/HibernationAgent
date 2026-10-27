"""Summarize isolated dialogue trials without pretending to score personality."""
from __future__ import annotations
import argparse
import difflib
import html
import json
from pathlib import Path
import statistics


def summarize(report: dict) -> dict:
    rows = [r for r in report["results"] if not r.get("seed")]
    attempts = [t for r in rows for t in (r.get("trace") or {}).get("turns", [])]
    reviews = [t for r in rows for t in (r.get("trace") or {}).get("reviews", [])]
    known_usage = [t for t in attempts + reviews if "input_tokens" in t or "output_tokens" in t]
    lengths = [len(r["reply"]) for r in rows if r.get("reply")]
    previous: dict[tuple, str] = {}
    repetition = []
    for row in rows:
        key = (row["case_id"], row["history"], row["review"])
        reply = row.get("reply") or ""
        old = previous.get(key, "")
        if old and reply:
            ratio = difflib.SequenceMatcher(None, old, reply, autojunk=False).ratio()
            if ratio >= .45:
                repetition.append({"case": row["case_id"], "turn": row["turn"], "character_overlap": round(ratio, 3)})
        previous[key] = reply
    return {
        "label": report["label"], "model": report.get("model"),
        "voice_variant": report.get("voice_variant", "configured"),
        "binary_sha256": report.get("binary_sha256"),
        "interaction_policies": sorted({r["trace"].get("interaction_policy", "unknown") for r in rows if r.get("trace")}),
        "continuous_drift_violations": sum(r.get("continuous_drift_valid") is False for r in rows),
        "unrequested_reference_artifacts": sum(bool(r.get("unrequested_reference_marker")) for r in rows),
        "measured_turns": len(rows), "total_calls_including_seeds": report["model_calls"],
        "private_consideration": [{
            "case": item.get("case_id"), "status": item.get("status"),
            "actual_activity_reflection": item.get("actual_activity_reflection", False),
            "model_calls": item.get("model_calls"),
            "successful_tools": [tool.get("tool") for tool in (item.get("trace") or {}).get("tool_results", []) if tool.get("ok")],
        } for item in report.get("solitude_observations", [])],
        "failed_turns": sum(bool(r.get("call_error")) for r in rows),
        "actor_attempts": len(attempts), "review_attempts": len(reviews),
        "format_failures": sum(t.get("status") == 10 for t in attempts),
        "normalized_envelopes": sum(bool(t.get("envelope_normalized")) for t in attempts),
        "plain_reply_candidates": sum(bool(t.get("plain_reply_wrapped")) for t in attempts),
        "review_rewrites": sum((t.get("verdict") or {}).get("decision") in ("rewrite", "reset") for t in reviews),
        "median_turn_ms": round(statistics.median(r["elapsed_ms"] for r in rows)) if rows else None,
        "median_reply_characters": round(statistics.median(lengths)) if lengths else None,
        "known_input_tokens": sum(t.get("input_tokens", 0) for t in known_usage),
        "known_output_tokens": sum(t.get("output_tokens", 0) for t in known_usage),
        "usage_records_available": len(known_usage), "usage_records_expected": len(attempts) + len(reviews),
        "repetition_for_manual_review": repetition,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reports = [json.loads(p.read_text(encoding="utf-8-sig")) for p in args.reports]
    if any(not r.get("isolated") or r.get("production_archive_read") for r in reports):
        raise ValueError("Only isolated trial reports can be rendered")
    summaries = [summarize(r) for r in reports]
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {"note": "描述性结果；小样本无统计显著性或人格质量评分。缺失用量不能当作零。", "reports": summaries}
    (args.output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    esc = lambda value: html.escape(str(value))
    parts = ["<!doctype html><html lang=zh-CN><meta charset=utf-8><title>小爱对话实验</title>",
             "<style>body{font:16px/1.7 system-ui;background:#f6f7f1;color:#263e35;margin:32px auto;max-width:1300px;padding:0 20px}h1{font-size:28px}table{border-collapse:collapse;width:100%;background:white}td,th{border:1px solid #dce5d7;padding:10px;text-align:left}section{margin:28px 0}.samples{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:16px}.sample{background:white;border:1px solid #dce5d7;border-radius:14px;padding:20px;white-space:pre-wrap}.input{color:#597968}.reply{margin-top:14px}small{color:#60766b}code{word-break:break-all}</style>",
             "<h1>小爱 · 独立对话实验</h1><p>以下都是新存档中的实际回复。调用与修复可计数；自然、连贯、有无自己的关注仍需逐例判断。字符重复只提示复核，不自动判为机械。模型采样可能产生变化，小样本不能单独证明因果。</p>",
             "<table><tr><th>版本</th><th>有效轮数</th><th>全部调用</th><th>失败轮</th><th>格式失败尝试</th><th>审核改写</th><th>中位耗时</th></tr>"]
    for s in summaries:
        parts.append("<tr>" + "".join(f"<td>{esc(v)}</td>" for v in [s["label"], s["measured_turns"], s["total_calls_including_seeds"], s["failed_turns"], s["format_failures"], s["review_rewrites"], f'{s["median_turn_ms"]} ms']) + "</tr>")
    parts.append("</table>")
    keys = sorted({(r["case_id"], r["history"], r["review"], r["turn"]) for report in reports for r in report["results"] if not r.get("seed")})
    for key in keys:
        parts.append(f"<section><h2>{esc(key[0])} · {esc(key[1])} · 审核 {esc(key[2])} · 第 {key[3] + 1} 轮</h2><div class=samples>")
        for report in reports:
            row = next((r for r in report["results"] if not r.get("seed") and (r["case_id"], r["history"], r["review"], r["turn"]) == key), None)
            if row is None:
                continue
            error = row.get("call_error")
            text = row.get("reply") or (f"调用失败：{error.get('code')}" if error else "本轮沉默")
            parts.append(f"<article class=sample><b>{esc(report['label'])}</b><div class=input>{esc(row['input'])}</div><div class=reply>{esc(text)}</div><small>{row['calls']} 次调用 · {row['elapsed_ms']} ms</small></article>")
        parts.append("</div></section>")
    parts.append("</html>")
    (args.output / "dialogue.html").write_text("".join(parts), encoding="utf-8")
    print(f"Rendered {len(keys)} comparison groups into {args.output.resolve()}")


if __name__ == "__main__":
    main()
