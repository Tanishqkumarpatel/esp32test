#!/usr/bin/env python3
"""Capture one mock exo run over USB serial, save it, and apply the pass check.

Stage 1 (stub comms): footer present, ticks == ticks_expected, overruns == 0,
                      log_dropped == 0. Tick timing is checked separately on the
                      logic analyzer.
Stage 2 (real comms): stage 1, plus send_errors == 0 and no stale ticks after
                      the first --stale-grace ticks.

Exit code 0 = pass, 1 = fail. An empty or footer-less log always fails.

Usage:
  pip install pyserial
  python3 capture.py --port /dev/cu.usbserial-XXXX --stage 1
Opening the port resets most ESP32 dev boards, which starts a fresh run.
"""
import argparse
import datetime
import pathlib
import sys

import serial

CSV_COLUMNS = "tick,t_us,closure,force_n,force_age_us,force_valid,stale,overrun,send_err"


def parse_kv(line):
    """'# key=a other=b' -> {'key': 'a', 'other': 'b'}"""
    return dict(tok.split("=", 1) for tok in line.lstrip("# ").split() if "=" in tok)


def capture(port, baud, timeout_s):
    """Read lines from the header until the footer (or timeout). Boot noise before the header is dropped."""
    lines = []
    seen_header = False
    with serial.Serial(port, baud, timeout=timeout_s) as ser:
        while True:
            raw = ser.readline()
            if not raw:  # read timed out
                break
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if line.startswith("# mock_exo"):
                seen_header = True
                lines = []
            if not seen_header:
                continue
            lines.append(line)
            if line.startswith("# end"):
                break
    return lines


def check(lines, stage, stale_grace):
    failures = []
    if not lines:
        return {}, {}, ["empty log: no header received"]

    header = parse_kv(lines[0])
    footer_lines = [l for l in lines if l.startswith("# end")]
    if not footer_lines:
        return header, {}, ["no footer: run did not finish or log was cut off"]
    footer = {k: int(v) for k, v in parse_kv(footer_lines[0]).items()}

    expected = int(header.get("ticks_expected", -1))
    if footer.get("ticks") != expected:
        failures.append(f"ticks={footer.get('ticks')} expected {expected}")
    for key in ("overruns", "log_dropped"):
        if footer.get(key, 0) != 0:
            failures.append(f"{key}={footer[key]}")

    rows = [l for l in lines[1:] if l and not l.startswith("#") and l != CSV_COLUMNS]
    # With log_dropped == 0 every tick should have a row.
    if footer.get("log_dropped", 0) == 0 and len(rows) != footer.get("ticks"):
        failures.append(f"{len(rows)} CSV rows but ticks={footer.get('ticks')}")

    if stage >= 2:
        if header.get("comms") != "real":
            failures.append("stage 2 needs firmware built with real comms (comms=stub in header)")
        if footer.get("send_errors", 0) != 0:
            failures.append(f"send_errors={footer['send_errors']}")
        late_stale = 0
        for row in rows:
            cols = row.split(",")
            if len(cols) == 9 and int(cols[0]) >= stale_grace and cols[6] == "1":
                late_stale += 1
        if late_stale:
            failures.append(f"{late_stale} stale ticks after the first {stale_grace}")

    return header, footer, failures


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=921600)  # matches sdkconfig.defaults
    ap.add_argument("--stage", type=int, choices=(1, 2), default=1)
    # TBD: "first few ticks" before the first force reply; adjust once we see real runs.
    ap.add_argument("--stale-grace", type=int, default=10)
    ap.add_argument("--timeout", type=float, default=3.0, help="seconds of silence before giving up")
    ap.add_argument("--out-dir", default="runs")
    args = ap.parse_args()

    wall = datetime.datetime.now()
    lines = capture(args.port, args.baud, args.timeout)

    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"run_{wall:%Y%m%d_%H%M%S}.log"
    # The board has no wall clock, so the laptop stamps it here.
    out.write_text(f"# captured_at={wall.isoformat()} port={args.port} stage={args.stage}\n"
                   + "\n".join(lines) + "\n")

    header, footer, failures = check(lines, args.stage, args.stale_grace)
    print(f"log: {out}")
    if header:
        print(f"scenario={header.get('scenario')} fw={header.get('fw')} comms={header.get('comms')}")
    if footer:
        print(" ".join(f"{k}={v}" for k, v in footer.items()))
    if failures:
        print(f"FAIL (stage {args.stage})")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print(f"PASS (stage {args.stage})")


if __name__ == "__main__":
    main()
