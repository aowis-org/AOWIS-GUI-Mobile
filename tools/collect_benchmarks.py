#!/usr/bin/env python3
"""Collect existing AOWIS Mobile benchmark captures and make them ZIP-reviewable.

No Android SDK, root, USB access in Docker, or manual file copying is needed.
Run captures in the application first; this tool never invents measurements.
"""

import argparse
import csv
import hashlib
import io
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "benchmarks"
PACKAGE = "org.aowis.mobile"
REPORT_NAME = re.compile(r"network-[0-9]+-[0-9]{8}-[0-9]{6}-[0-9]{3}\.csv\Z")
MEASUREMENTS = ("interval_us", "submit_us", "gpu_us", "draws", "visible_tiles", "uploads",
                "lod_stride", "nodes_drawn", "pipes_drawn", "vertex_buffer_bytes")


def run(args, *, timeout=40):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, check=False)


def text_result(args, *, timeout=15):
    result = run(args, timeout=timeout)
    return result.stdout.decode("utf-8", "replace").strip() if result.returncode == 0 else ""


def valid_report(content):
    return content.startswith(b"# AOWIS Mobile GPU benchmark,")


def save_unchanged_or_new(target, content):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file():
        if target.read_bytes() == content:
            return False
        digest = hashlib.sha256(content).hexdigest()[:12]
        target = target.with_name(f"{target.stem}-collision-{digest}{target.suffix}")
        if target.is_file() and target.read_bytes() == content:
            return False
    tmp = target.with_name(f".{target.name}.temporary-{os.getpid()}")
    try:
        tmp.write_bytes(content)
        tmp.replace(target)
    finally:
        tmp.unlink(missing_ok=True)
    return True


def desktop_sources(extra_dirs):
    base = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    names = ("AOWIS Mobile", "AOWIS", "AOWIS-GUI-Mobile", "org.aowis.mobile")
    dirs = [base / name / "benchmarks" for name in names]
    dirs += [Path(d).expanduser() for d in extra_dirs]
    seen = set()
    for directory in dirs:
        directory = directory.resolve()
        if directory in seen or not directory.is_dir():
            continue
        seen.add(directory)
        for source in sorted(directory.glob("network-*.csv")):
            if source.is_file() and not source.is_symlink() and REPORT_NAME.fullmatch(source.name):
                yield source


def collect_desktop(extra_dirs):
    found = 0
    copied = 0
    for source in desktop_sources(extra_dirs):
        data = source.read_bytes()
        if not valid_report(data):
            print(f"Skipping unexpected desktop report: {source}", file=sys.stderr)
            continue
        found += 1
        copied += save_unchanged_or_new(BENCH / "desktop" / source.name, data)
    print(f"Linux: {found} app reports found; {copied} new saved in benchmarks/desktop")
    return found


def available_devices(adb):
    result = run([adb, "devices", "-l"], timeout=20)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode("utf-8", "replace").strip() or "adb devices failed")
    authorized = []
    for line in result.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            authorized.append(parts[0])
    return authorized


def entries_from_tar(payload, expected):
    # Don't trust paths in device-provided tar archives; never extract arbitrary
    # paths, links, or directories onto the host.
    extracted = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:") as archive:
        for member in archive:
            name = Path(member.name).name
            if not member.isfile() or member.name.removeprefix("./") != name:
                continue
            if not expected(name) or member.size > 16 * 1024 * 1024:
                continue
            source = archive.extractfile(member)
            if source is None:
                continue
            content = source.read()
            if len(content) == member.size:
                extracted[name] = content
    return extracted


def adb_tar(adb, serial, directory):
    command = [adb, "-s", serial, "exec-out", "run-as", PACKAGE,
               "tar", "-C", directory, "-cf", "-", "."]
    result = run(command, timeout=80)
    if result.returncode != 0:
        error = result.stderr.decode("utf-8", "replace").strip()
        raise RuntimeError(error or f"Android run-as failed for {directory}")
    return result.stdout


def collect_device(adb, serial):
    command = [adb, "-s", serial]
    installed = text_result(command + ["shell", "pm", "path", PACKAGE])
    if not installed.startswith("package:"):
        raise RuntimeError("AOWIS debug app not installed; run ./compile_android.sh --run")
    model = text_result(command + ["shell", "getprop", "ro.product.model"]) or "Android"
    os_version = text_result(command + ["shell", "getprop", "ro.build.version.release"]) or "unknown"
    model_slug = re.sub(r"[^A-Za-z0-9._-]+", "-", model).strip("-._")[:32] or "Android"
    digest = hashlib.sha256(serial.encode()).hexdigest()[:10]
    key = f"{model_slug}-{digest}"
    directory = BENCH / "android" / key
    tar_bytes = adb_tar(adb, serial, "files/benchmarks")
    reports = entries_from_tar(tar_bytes, lambda name: REPORT_NAME.fullmatch(name) is not None)
    copied = 0
    for name, content in sorted(reports.items()):
        if valid_report(content):
            copied += save_unchanged_or_new(directory / name, content)
    if reports:
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "device.txt").write_text(
            f"Model: {model}\nAndroid: {os_version}\nPackage: {PACKAGE}\n"
            "ADB identity is omitted from this shared report.\n", encoding="utf-8")
    print(f"Android ({model}, {os_version}): {len(reports)} reports found; {copied} new -> {directory.relative_to(ROOT)}")
    try:
        lifecycle = entries_from_tar(adb_tar(adb, serial, "files/lifecycle"),
                                     lambda name: name == "events.csv")
        if "events.csv" in lifecycle:
            target = BENCH / "lifecycle" / key / "events.csv"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(lifecycle["events.csv"])
            print(f"  Lifecycle log -> {target.relative_to(ROOT)}")
    except (RuntimeError, tarfile.TarError) as error:
        print(f"  Lifecycle log unavailable: {error}")
    return len(reports)


def percentile(numbers, p):
    values = sorted(numbers)
    return values[max(0, math.ceil(p * len(values)) - 1)] if values else None


def ms(value):
    return f"{value / 1000:.3f}" if value is not None else ""


def numeric(values, column, *, minimum=None):
    result = []
    for sample in values:
        try:
            value = int(sample[column])
            if minimum is None or value >= minimum:
                result.append(value)
        except (ValueError, TypeError, KeyError):
            pass
    return result


def describe_report(path):
    with path.open("r", newline="", encoding="utf-8") as file:
        lines = file.readlines()
    headers = [line.strip() for line in lines if line.startswith("#")]
    content = "".join(line for line in lines if not line.startswith("#") and line.strip())
    reader = csv.DictReader(io.StringIO(content))
    if not reader.fieldnames or any(key not in reader.fieldnames for key in MEASUREMENTS):
        raise ValueError("Unexpected frame CSV columns")
    samples = list(reader)
    if not samples:
        raise ValueError("No frame samples")
    preset = next((re.search(r"Preset:\s*(\d+)", line).group(1)
                   for line in headers if re.search(r"Preset:\s*(\d+)", line)), "?")
    logical = next((re.search(r"total=(\d+)", line).group(1)
                    for line in headers if re.search(r"total=(\d+)", line)), "?")
    device = next((line.partition("Device:")[2].strip() for line in headers
                   if "Device:" in line), "unknown")
    intervals = numeric(samples, "interval_us", minimum=1)
    if not intervals:
        raise ValueError("No valid frame intervals")
    visible = [a+b for a, b in zip(numeric(samples, "nodes_drawn"),
                                   numeric(samples, "pipes_drawn"))]
    relative = path.relative_to(ROOT).as_posix()
    return {
        "platform": path.parts[len(ROOT.parts) + 1],
        "device": device, "preset": preset, "logical_features": logical,
        "frames": str(len(samples)), "p50_ms": ms(percentile(intervals, .5)),
        "p95_ms": ms(percentile(intervals, .95)),
        "p99_ms": ms(percentile(intervals, .99)),
        "cpu_p95_ms": ms(percentile(numeric(samples, "submit_us", minimum=0), .95)),
        "gpu_p95_ms": ms(percentile(numeric(samples, "gpu_us", minimum=0), .95)),
        "peak_visible_features": str(max(visible, default=0)),
        "peak_tiles": str(max(numeric(samples, "visible_tiles"), default=0)),
        "tile_uploads": str(sum(numeric(samples, "uploads", minimum=0))),
        "max_lod_stride": str(max(numeric(samples, "lod_stride"), default=1)),
        "source": relative,
    }


def write_summary():
    reports = sorted((BENCH / "desktop").glob("network-*.csv"))
    reports += sorted((BENCH / "android").rglob("network-*.csv"))
    rows = []
    for file in reports:
        if not file.is_file() or file.is_symlink():
            continue
        try:
            rows.append(describe_report(file))
        except (ValueError, OSError, UnicodeError) as error:
            print(f"Skipping invalid report {file}: {error}", file=sys.stderr)
    BENCH.mkdir(exist_ok=True)
    headers = ("platform", "device", "preset", "logical_features", "frames", "p50_ms",
               "p95_ms", "p99_ms", "cpu_p95_ms", "gpu_p95_ms",
               "peak_visible_features", "peak_tiles", "tile_uploads", "max_lod_stride", "source")
    with (BENCH / "summary.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)
    lines = ["# AOWIS Mobile GPU benchmark recordings", "",
             f"Collected: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}", "",
             "Results below come from real application CSV recordings, not generated benchmarks.",
             "Uncontrolled interactions, device refresh rates and camera zoom can differ.",
             "Logical feature counts are not the number simultaneously drawn (viewport culling / LOD).", "",
             "| Platform | Device | Preset | Frames | p50 ms | p95 ms | p99 ms | GPU p95 ms | Peak visible | Source |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"]
    for row in rows:
        lines.append("| " + " | ".join((row["platform"], row["device"].replace("|", "/"),
              row["preset"], row["frames"], row["p50_ms"], row["p95_ms"],
              row["p99_ms"], row["gpu_p95_ms"] or "N/A", row["peak_visible_features"],
              f"`{row['source']}`")) + " |")
    lines += ["", "GPU timing is N/A where bgfx/driver did not supply it.",
              "For complete frame data and CPU/LOD/tile details, open the referenced CSV files.", ""]
    (BENCH / "summary.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"Summary: {len(rows)} recordings -> benchmarks/summary.csv and summary.md")
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description="Collect Linux + Android AOWIS GPU benchmark CSVs and refresh mobile.zip")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--desktop-only", action="store_true", help="Skip Android ADB")
    group.add_argument("--android-only", action="store_true", help="Skip Linux data")
    parser.add_argument("--serial", action="append", default=[], help="Collect from this authorized Android device (repeatable)")
    parser.add_argument("--desktop-dir", action="append", default=[], help="Additional Linux app benchmarks directory")
    parser.add_argument("--strict", action="store_true", help="Exit nonzero if requested device/platform has no recordings")
    parser.add_argument("--no-zip", action="store_true", help="Do not regenerate parent mobile.zip")
    parser.add_argument("--run-lifecycle", action="store_true", help="Also run Android Home/resume lifecycle tests")
    parser.add_argument("--cycles", type=int, default=12, help="Lifecycle cycles with --run-lifecycle (1-100)")
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100:
        parser.error("--cycles must be from 1 to 100")
    if args.run_lifecycle and args.desktop_only:
        parser.error("--run-lifecycle cannot be combined with --desktop-only")

    missing = []
    if not args.android_only:
        if collect_desktop(args.desktop_dir) == 0:
            missing.append("Linux: no app benchmark CSVs found (capture with Record 15s)")
    if not args.desktop_only:
        adb = shutil.which("adb")
        if not adb:
            missing.append("Android: host adb not installed")
        else:
            try:
                devices = available_devices(adb)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                devices = []
                missing.append(f"Android ADB error: {error}")
            if args.serial:
                absent = set(args.serial) - set(devices)
                for serial in sorted(absent):
                    missing.append(f"Android device not authorized/connected: {serial}")
                devices = [serial for serial in devices if serial in args.serial]
            if not devices:
                missing.append("Android: no authorized USB or paired Wi-Fi devices")
            for serial in devices:
                if args.run_lifecycle:
                    command = [str(ROOT / "tools/test_android_lifecycle.sh"),
                               "--serial", serial, "--cycles", str(args.cycles)]
                    print(f"Running lifecycle tests on {serial}...")
                    result = subprocess.run(command, check=False)
                    if result.returncode:
                        missing.append(f"Android lifecycle test failed on {serial}")
                try:
                    if collect_device(adb, serial) == 0:
                        missing.append(f"Android {serial}: no app benchmark CSV recordings")
                except (RuntimeError, subprocess.TimeoutExpired, tarfile.TarError, OSError) as error:
                    missing.append(f"Android {serial}: {error}")
    count = write_summary()
    for warning in missing:
        print(f"NOTICE: {warning}", file=sys.stderr)
    if not args.no_zip:
        result = subprocess.run([str(ROOT / "tools/zip_mobile_project.sh")], check=False)
        if result.returncode:
            print("ERROR: refreshing mobile.zip failed", file=sys.stderr)
            return 1
    print(f"Ready: {count} recordings in project benchmarks/; upload refreshed mobile.zip")
    return 1 if args.strict and missing else 0


if __name__ == "__main__":
    sys.exit(main())
