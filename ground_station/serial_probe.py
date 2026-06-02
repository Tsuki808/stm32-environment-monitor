from __future__ import annotations

import argparse
import sys
import time

import serial


def calc_checksum(payload: str) -> int:
    checksum = 0
    for ch in payload:
        checksum ^= ord(ch)
    return checksum


def build_cmd_frame(command: str) -> bytes:
    clean = command.strip()
    if clean.startswith("@") and "*" in clean:
        frame = clean.rstrip("\r\n")
    else:
        payload = clean.split("*", 1)[0] if clean.startswith("@") else f"@CMD,{clean}"
        frame = f"{payload}*{calc_checksum(payload):02X}"
    return f"{frame}\r\n".encode("ascii")


def verify_frame(line: str) -> tuple[bool, str]:
    clean = line.strip()
    if not clean:
        return True, "empty"
    if not clean.startswith("@"):
        return True, "raw"
    if "*" not in clean:
        return False, "missing checksum"
    payload, checksum_text = clean.rsplit("*", 1)
    try:
        received = int(checksum_text[:2], 16)
    except ValueError:
        return False, "bad checksum text"
    calculated = calc_checksum(payload)
    if received != calculated:
        return False, f"checksum mismatch rx={received:02X} calc={calculated:02X}"
    return True, "ok"


def parse_args():
    parser = argparse.ArgumentParser(description="Serial probe for STM32F103 ground-station protocol.")
    parser.add_argument("--port", default="COM3", help="PC-side virtual COM port, default COM3")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate, default 115200")
    parser.add_argument("--cmd", default="STAT?", help="Command to send. Use --no-cmd for passive monitor.")
    parser.add_argument("--no-cmd", action="store_true", help="Do not send a command; only monitor RX")
    parser.add_argument("--seconds", type=float, default=5.0, help="Read duration in seconds")
    parser.add_argument("--raw", action="store_true", help="Print raw bytes instead of decoded lines")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    print(f"OPEN port={args.port} baud={args.baud} mode=8N1")

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.2,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as ser:
            ser.setRTS(False)
            ser.setDTR(False)
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            if not args.no_cmd:
                frame = build_cmd_frame(args.cmd)
                written = ser.write(frame)
                ser.flush()
                print(f"TX {frame!r} written={written}")
            else:
                print("TX skipped (--no-cmd)")

            print(f"LINES CTS={int(ser.cts)} DSR={int(ser.dsr)} RTS={int(ser.rts)} DTR={int(ser.dtr)}")
            deadline = time.time() + args.seconds
            total = 0
            bad = 0

            while time.time() < deadline:
                data = ser.read_until(b"\n")
                if not data:
                    continue
                total += len(data)
                if args.raw:
                    print(f"RX raw {data!r}")
                    continue
                text = data.decode("ascii", errors="replace").strip()
                ok, reason = verify_frame(text)
                if not ok:
                    bad += 1
                print(f"RX {'OK ' if ok else 'BAD'} {reason}: {text}")

            print(f"DONE rx_bytes={total} bad_frames={bad}")
            return 1 if bad else 0
    except serial.SerialException as exc:
        print(f"ERROR serial open/read failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
