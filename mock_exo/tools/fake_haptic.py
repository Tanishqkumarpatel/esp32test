#!/usr/bin/env python3
"""Fake haptic task: emit closure frames at a fixed rate, with a configurable send latency
and a configurable share of frames carrying a wrong CRC, to exercise the comms receiver.

Frame (15-byte packet, then COBS, then a 0x00 delimiter = 17 bytes for a 15-byte packet
with no run of 254 non-zero bytes):
  type u8 | seq u16 | ack_seq u16 | time_us u32 | payload f32 | crc16 u16   (little-endian)

Latency: each closure is measured at tick N and sent at tick N + latency. time_us keeps the
measurement time, so the receiver sees the closure aged by the latency.

Bad CRC: a frame picked for corruption gets its CRC XORed with a non-zero value, so it is
always wrong. The rest of the frame is left intact, so the receiver should drop it on the
CRC check alone. Pick frames with --crc-error-rate (random) and/or --crc-error-every N.

TBD (assumed until task 3 publishes the packet spec): little-endian fields, type 0x01 for
closure, ack_seq 0, CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over the bytes before it.

Usage:
  python3 fake_haptic.py --latency-ms 5 --crc-error-rate 0.05 --duration-s 5   # hex to stdout
  python3 fake_haptic.py --port /dev/cu.usbserial-XXXX --baud 921600 --crc-error-every 100
"""
import argparse
import random
import struct
import sys
import time

TYPE_CLOSURE = 0x01  # TBD: packet type codes are not defined yet

# Same shape as main/scenario.c: open 0.5 s, squeeze 2 s, hold 1 s, release 2 s.
OPEN_MS, SQUEEZE_MS, HOLD_MS, RELEASE_MS = 500, 2000, 1000, 2000
CLOSURE_OPEN, CLOSURE_SQUEEZED = 0.0, 0.8  # TBD placeholders, as in scenario.c


def closure_at(t_ms):
    """Slow squeeze, repeating."""
    t_ms %= OPEN_MS + SQUEEZE_MS + HOLD_MS + RELEASE_MS
    if t_ms < OPEN_MS:
        return CLOSURE_OPEN
    t_ms -= OPEN_MS
    if t_ms < SQUEEZE_MS:
        return CLOSURE_OPEN + (CLOSURE_SQUEEZED - CLOSURE_OPEN) * t_ms / SQUEEZE_MS
    t_ms -= SQUEEZE_MS
    if t_ms < HOLD_MS:
        return CLOSURE_SQUEEZED
    t_ms -= HOLD_MS
    return CLOSURE_SQUEEZED - (CLOSURE_SQUEEZED - CLOSURE_OPEN) * t_ms / RELEASE_MS


def crc16_ccitt_false(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data):
    out = bytearray([0])
    code_i, code = 0, 1
    for b in data:
        if b == 0:
            out[code_i] = code
            code_i, code = len(out), 1
            out.append(0)
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[code_i] = code
                code_i, code = len(out), 1
                out.append(0)
    out[code_i] = code
    return bytes(out)


def build_frame(seq, time_us, closure, ack_seq=0, bad_crc=0):
    """bad_crc: non-zero value XORed into the CRC. 0 leaves it correct."""
    body = struct.pack("<BHHIf", TYPE_CLOSURE, seq & 0xFFFF, ack_seq, time_us & 0xFFFFFFFF, closure)
    crc = crc16_ccitt_false(body) ^ bad_crc
    return cobs_encode(body + struct.pack("<H", crc)) + b"\x00"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rate-hz", type=float, default=1000.0, help="frames per second (deck slide 9: 1 kHz)")
    ap.add_argument("--latency-ms", type=float, default=0.0,
                    help="delay between measuring closure and sending it, rounded to whole ticks")
    ap.add_argument("--crc-error-rate", type=float, default=0.0, help="probability 0..1 that a frame gets a wrong CRC")
    ap.add_argument("--crc-error-every", type=int, default=0, help="also corrupt every Nth frame (0 = off)")
    ap.add_argument("--duration-s", type=float, default=5.0)
    ap.add_argument("--seed", type=int, default=None, help="fix the random choice of corrupted frames")
    ap.add_argument("--port", help="serial port to write frames to; omit to print hex lines to stdout")
    ap.add_argument("--baud", type=int, default=921600)
    args = ap.parse_args()

    if not 0.0 <= args.crc_error_rate <= 1.0:
        ap.error("--crc-error-rate must be between 0 and 1")
    if args.rate_hz <= 0 or args.latency_ms < 0 or args.crc_error_every < 0:
        ap.error("rate must be > 0; latency and --crc-error-every must be >= 0")

    rng = random.Random(args.seed)
    period = 1.0 / args.rate_hz
    latency_ticks = round(args.latency_ms * args.rate_hz / 1000.0)
    total = int(args.duration_s * args.rate_hz)

    ser = None
    if args.port:
        import serial  # pip install pyserial
        ser = serial.Serial(args.port, args.baud)

    delay_line = []  # (time_us, closure) measured but not yet sent
    sent = bad = 0
    t0 = time.perf_counter()
    for tick in range(total):
        # Pace against absolute deadlines so one slow iteration does not shift the rest.
        deadline = t0 + tick * period
        while time.perf_counter() < deadline:
            pass

        time_us = int((time.perf_counter() - t0) * 1e6)
        delay_line.append((time_us, closure_at(int(tick * 1000 / args.rate_hz))))
        if len(delay_line) <= latency_ticks:
            continue  # still filling: nothing is sent for the first latency ticks
        meas_us, closure = delay_line.pop(0)

        corrupt = rng.random() < args.crc_error_rate
        if args.crc_error_every and (sent + 1) % args.crc_error_every == 0:
            corrupt = True
        frame = build_frame(sent, meas_us, closure, bad_crc=rng.randint(1, 0xFFFF) if corrupt else 0)

        if ser:
            ser.write(frame)
        else:
            print(frame.hex(), "BAD_CRC" if corrupt else "")
        sent += 1
        bad += corrupt

    if ser:
        ser.close()
    print(f"sent={sent} bad_crc={bad} latency_ticks={latency_ticks} rate_hz={args.rate_hz:g}", file=sys.stderr)


if __name__ == "__main__":
    main()
