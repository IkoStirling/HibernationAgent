"""Render metadata-only evidence for one bounded continuity experiment.

Reads only explicit experiment status/metric files. It never opens runtime
configuration, credentials, conversation archives, or encrypted trace bodies.
"""
from __future__ import annotations

import argparse
import html
import json
import math
import statistics
from datetime import datetime, timezone
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def number(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def samples(root: Path):
    rows, unreadable = [], 0
    try:
        lines = (root / "samples.jsonl").read_text(encoding="utf-8-sig").splitlines()
    except OSError:
        return rows, unreadable
    for line in lines:
        try:
            row = json.loads(line)
            if not isinstance(row, dict) or number(row.get("real_elapsed_seconds")) is None:
                raise ValueError("invalid metric row")
            rows.append(row)
        except ValueError:
            unreadable += 1
    return rows, unreadable


def series(rows, name, factor=1):
    xs, ys, previous = [], [], None
    for row in rows:
        current = row["real_elapsed_seconds"]
        if previous is not None and current - previous > 120:
            xs.append(current / 3600)
            ys.append(float("nan"))
        value = number(row.get(name))
        xs.append(current / 3600)
        ys.append(value * factor if value is not None else float("nan"))
        previous = current
    return xs, ys


def escaped(value):
    return html.escape(str(value))


def main_observations(root: Path):
    rows, unreadable = [], 0
    try:
        lines = (root / "main-metadata-samples.jsonl").read_text(encoding="utf-8-sig").splitlines()
    except OSError:
        lines = []
    for line in lines:
        try:
            row = json.loads(line)
            if not isinstance(row, dict) or not isinstance(row.get("observed_utc"), str):
                raise ValueError("invalid main observation")
            rows.append(row)
        except ValueError:
            unreadable += 1
    def values(key):
        return sorted({str(row[key]) for row in rows if row.get(key) not in (None, "")})
    try:
        observation_errors = sum(bool(line.strip()) for line in (root / "main-metadata-errors.jsonl").read_text(encoding="utf-8-sig").splitlines())
    except OSError:
        observation_errors = 0
    return {
        "samples": len(rows), "unreadable_sample_lines": unreadable,
        "status_observation_errors": observation_errors,
        "first_observed_utc": rows[0]["observed_utc"] if rows else None,
        "last_observed_utc": rows[-1]["observed_utc"] if rows else None,
        "versions": values("version"), "health_codes": values("health"),
        "process_ids": values("pid"), "runtime_codes": values("runtime_code"),
        "qq_issues": values("qq_issue"),
        "cognition_load_error_samples": sum(row.get("cognition_load_error") is True for row in rows),
        "plugin_failure_samples": sum((number(row.get("plugin_failure_count")) or 0) > 0 for row in rows),
        "max_audit_dropped": max((number(row.get("audit_dropped")) or 0 for row in rows), default=None),
        "read_only_public_metadata": True,
        "observer_status": read_json(root / "main-metadata-status.json"),
    }


def render(root: Path):
    root = root.resolve(strict=True)
    session = read_json(root / "session.json")
    meter = read_json(root / "meter-status.json")
    now = datetime.now(timezone.utc)
    start = datetime.fromisoformat(session["start_utc"].replace("Z", "+00:00"))
    end = datetime.fromisoformat(session["end_utc"].replace("Z", "+00:00"))
    window_elapsed = max(0, (now - start).total_seconds()) / 3600
    font = Path("C:/Windows/Fonts/msyh.ttc")
    if font.exists():
        from matplotlib import font_manager
        font_manager.fontManager.addfont(str(font))
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=str(font)).get_name()
    plt.rcParams["axes.unicode_minus"] = False
    cohorts, summaries = [], []
    for directory in sorted(root.glob("soak-*")):
        if not directory.is_dir():
            continue
        status = read_json(directory / "status.json")
        if not status:
            continue
        rows, unreadable = samples(directory)
        cohorts.append((directory.name, status, rows))
        summaries.append({
            "name": directory.name,
            "description": session.get("cohort_descriptions", {}).get(directory.name, directory.name),
            "runtime_sha256": status.get("runtime_sha256"),
            "started_utc": status.get("started_utc"),
            "observed_utc": status.get("observed_utc"),
            "observed_seconds": number(status.get("real_elapsed_seconds")),
            "samples": len(rows), "unreadable_sample_lines": unreadable,
            "finished": status.get("finished") is True,
            "interventions": number(status.get("interventions")),
            "failure_count": len(status.get("failures", [])),
            "failures": status.get("failures", []),
            "cycle_woke_at_seconds": number(status.get("cycle_woke_at_seconds")),
            "cycle_reslept_at_seconds": number(status.get("cycle_reslept_at_seconds")),
            "natural_window_end": status.get("natural_window_end") is True,
            "window_end_utc": status.get("window_end_utc"),
            "window_ended_at_seconds": number(status.get("window_ended_at_seconds")),
        })
    fig, axes = plt.subplots(4, 1, figsize=(11, 11), constrained_layout=True)
    phases = {"idle": 0, "queued": 1, "requesting": 2, "sleeping": 3}
    for index, (name, status, rows) in enumerate(cohorts):
        label = session.get("cohort_descriptions", {}).get(name, f"{index + 1}: {name}")
        color = f"C{index % 10}"
        phase_rows = [dict(row, phase_value=phases.get(row.get("phase"))) for row in rows]
        axes[0].step(*series(phase_rows, "phase_value"), where="post", label=label, color=color)
        axes[1].plot(*series(rows, "sleep_inertia"), label=label, color=color)
        axes[2].plot(*series(rows, "private_bytes", 1 / 1048576), label=label, color=color)
        axes[3].plot(*series(rows, "gdi_objects"), label=label + " GDI", color=color)
        axes[3].plot(*series(rows, "handles"), label=label + " handles", color=color, linestyle="--")
    axes[0].set_yticks(list(phases.values()), ("清醒", "排队", "等待姿态", "睡眠"))
    axes[0].set_title("实际进程样本：不同构建分别计时，不补齐未运行时段")
    for axis, label in zip(axes, ("作息阶段", "连续睡意 / 惯性", "进程专用内存 MiB", "资源对象数")):
        axis.set_ylabel(label)
        axis.grid(alpha=.2)
        axis.legend(fontsize=8, loc="best")
        axis.set_xlim(left=0)
    axes[1].set_ylim(0, 1)
    axes[-1].set_xlabel("该进程启动后的实际小时；超过 120 秒的采样空隙断开")
    chart = root / "continuity-metrics.png"
    fig.savefig(chart, dpi=145)
    plt.close(fig)
    calls = number(meter.get("calls"))
    ledger = read_json(root / "budget.json")
    if calls is None:
        entries = ledger.get("entries")
        calls = len(entries) if isinstance(entries, list) else None
    if calls is None:
        calls = number(session.get("calls_used"))
    cache_groups = {}
    for entry in ledger.get("entries", []):
        if not isinstance(entry, dict):
            continue
        key = str(entry.get("route", "unknown"))
        group = cache_groups.setdefault(key, {"calls": 0, "reported_input_tokens": 0, "reported_output_tokens": 0, "cache_reported_calls": 0, "cached_input_tokens": 0, "uncached_input_tokens": 0, "system_versions": set(), "unknown_usage_calls": 0, "http_errors": 0, "latency_ms": []})
        group["calls"] += 1
        status = number(entry.get("http_status"))
        if status is not None and status >= 400:
            group["http_errors"] += 1
        latency = number(entry.get("elapsed_ms"))
        if latency is not None and math.isfinite(latency) and latency >= 0:
            group["latency_ms"].append(latency)
        group["system_versions"].add(str(entry.get("system_sha256", "unknown")))
        if entry.get("state") == "reported":
            group["reported_input_tokens"] += number(entry.get("input_tokens")) or 0
            group["reported_output_tokens"] += number(entry.get("output_tokens")) or 0
        else:
            group["unknown_usage_calls"] += 1
        if entry.get("cache_reported") is True:
            group["cache_reported_calls"] += 1
            group["cached_input_tokens"] += number(entry.get("cached_input_tokens")) or 0
            group["uncached_input_tokens"] += number(entry.get("uncached_input_tokens")) or 0
    for group in cache_groups.values():
        group["system_versions"] = len(group["system_versions"])
        total = group["cached_input_tokens"] + group["uncached_input_tokens"]
        group["reported_cache_ratio"] = group["cached_input_tokens"] / total if total else None
        ordered = sorted(group.pop("latency_ms"))
        group["latency_reported_calls"] = len(ordered)
        group["median_latency_ms"] = statistics.median(ordered) if ordered else None
        group["p95_latency_ms"] = ordered[math.ceil(len(ordered) * .95) - 1] if ordered else None
    report = {
        "schema_version": 1, "generated_utc": now.isoformat(),
        "authorized_start_utc": start.isoformat(), "authorized_end_utc": end.isoformat(),
        "window_elapsed_hours": window_elapsed,
        "window_has_elapsed": now >= end,
        "goal_completion_verified": now >= end and session.get("completion_verified") is True,
        "external_model_requests": calls,
        "external_model_limit": session.get("max_validation_api_requests"),
        "live_validation": session.get("checks", {}).get("live_validation", "unknown"),
        "checks": session.get("checks", {}), "cohorts": summaries,
        "production_observation": main_observations(root),
        "model_cache_by_route": cache_groups,
        "semantic_validation_claimed": False,
    }
    (root / "report-metadata.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    rows_html = "".join(
        "<tr>" + "".join(f"<td>{escaped(value)}</td>" for value in (
            item["description"], round((item["observed_seconds"] or 0) / 3600, 3),
            item["samples"], item["interventions"], item["failure_count"],
            "已结束" if item["finished"] else "进行中",
            (item["runtime_sha256"] or "未知")[:16],
        )) + "</tr>" for item in summaries
    )
    checks_html = "".join(f"<tr><td>{escaped(key)}</td><td>{escaped(value)}</td></tr>" for key, value in report["checks"].items())
    boundaries_html = "".join(
        f"<li>{escaped(item['name'])}：预定窗口结束 {escaped(item['window_end_utc'])}；"
        + (f"在启动后 {item['window_ended_at_seconds'] / 60:.2f} 个实际分钟观察到自然结束。"
           if item["window_ended_at_seconds"] is not None else "尚未观察到自然结束。")
        + "</li>" for item in summaries if item["natural_window_end"]
    )
    main = report["production_observation"]
    main_html = "".join(f"<tr><td>{escaped(label)}</td><td>{escaped(value)}</td></tr>" for label, value in (
        ("样本 / 不完整样本", f"{main['samples']} / {main['unreadable_sample_lines']}"),
        ("实际观察起止（UTC）", f"{main['first_observed_utc']} → {main['last_observed_utc']}"),
        ("程序版本 / 健康码", f"{', '.join(main['versions'])} / {', '.join(main['health_codes'])}"),
        ("认知加载错误 / 插件故障样本", f"{main['cognition_load_error_samples']} / {main['plugin_failure_samples']}"),
        ("只读采样失败次数", main["status_observation_errors"]),
        ("曾观察到的运行条件", ", ".join(main["runtime_codes"])),
        ("QQ 外部接口状态", ", ".join(main["qq_issues"]) or "未报告问题或尚无样本"),
    ))
    cache_html = "".join("<tr>" + "".join(f"<td>{escaped(value)}</td>" for value in (
        key, group["calls"], group["reported_input_tokens"], group["cache_reported_calls"],
        f"{group['reported_cache_ratio']:.1%}" if group["reported_cache_ratio"] is not None else "未知",
        group["unknown_usage_calls"],
        group["median_latency_ms"] if group["median_latency_ms"] is not None else "未知",
        group["http_errors"],
    )) + "</tr>" for key, group in sorted(cache_groups.items()))
    page = f"""<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>小爱连续运行验证</title>
<style>body{{font:16px/1.65 'Microsoft YaHei',sans-serif;background:#f5f6f0;color:#283e35;max-width:1120px;margin:35px auto;padding:20px}}h1{{font-size:26px}}.card{{background:white;padding:22px;border-radius:14px;margin:18px 0}}table{{width:100%;border-collapse:collapse;font-size:14px}}td,th{{text-align:left;padding:9px;border-bottom:1px solid #e3e9df;overflow-wrap:anywhere}}img{{width:100%;height:auto}}small{{color:#63736c}}code{{overflow-wrap:anywhere}}</style>
<h1>小爱 · 连续运行验证</h1><small>生成于 {escaped(now.isoformat())}</small>
<div class="card"><b>任务窗口已过去 {window_elapsed:.2f} / 8 小时。</b>
<p>任务窗口和各测试进程的实际运行时长分别记录。窗口到期不会自动判定工作完成。</p>
<p>人工收尾验收：{'已完成，详见检查记录及已知限制' if report['goal_completion_verified'] else '尚未完成'}。</p>
<p>真实外部模型请求：{escaped(calls if calls is not None else '未知')} / {escaped(session.get('max_validation_api_requests'))}。
本地模拟响应不计为真实模型验证；未请求或服务未提供缓存统计时，不报告缓存命中率。</p>
<p>当前真实模型验证状态：{escaped(report['live_validation'])}。</p></div>
<div class="card"><h2>供应商实际缓存统计</h2><table><tr><th>实验路由</th><th>请求</th><th>已报告输入 token</th><th>有缓存统计的请求</th><th>已报告输入命中率</th><th>用量未知</th><th>中位延迟 ms</th><th>HTTP 错误</th></tr>{cache_html}</table>
<p>包括冷启动、审核、后台规划及重试。角色／审核分开统计，未提供统计的调用不当作未命中。相邻请求的本地字节前缀只作排查指标，不等于供应商命中。</p></div>
<div class="card"><h2>长时进程观察</h2><table><tr><th>组别</th><th>实际小时</th><th>样本</th><th>干预</th><th>失败</th><th>状态</th><th>构建 SHA 前缀</th></tr>{rows_html}</table>
<ul>{boundaries_html}</ul>
<img src="continuity-metrics.png" alt="作息、惯性与进程资源的真实采样曲线"></div>
<div class="card"><h2>更新后主实例的只读观察</h2><table>{main_html}</table>
<p>仅读取公开状态、计数与资源，不读取聊天正文，不向主实例发送测试对话。正常业务用量与隔离验证账本分别计算；后台预算等待、NapCat 不可达与程序加载故障分别记录。</p></div>
<div class="card"><h2>独立验证记录</h2><table>{checks_html}</table>
<p>这些检查验证程序门禁、状态连续性、持久化、实际窗口动作及可追溯记录。模拟模型不能证明真实模型的人格、判断质量或主观体验。</p>
<p>完整构建标识、时间和采样缺失计数见同目录 report-metadata.json；受保护的原始记忆与私有判断内容未在此报告中解密或展示。</p></div></html>"""
    (root / "report.html").write_text(page, encoding="utf-8")
    print(json.dumps({"report": str(root / "report.html"), "cohorts": len(cohorts), "external_requests": calls}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("session_root", type=Path)
    render(parser.parse_args().session_root)
