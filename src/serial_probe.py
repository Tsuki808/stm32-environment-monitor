import argparse
import time

import serial


def calc_checksum(payload):
    cs = 0
    for ch in payload:
        cs ^= ord(ch)
    return cs


def build_cmd_frame(cmd):
    payload = cmd.split("*", 1)[0] if cmd.startswith("@") and "*" in cmd else f"@CMD,{cmd}"
    return f"{payload}*{calc_checksum(payload):02X}\r\n".encode("ascii")


def main():
    parser = argparse.ArgumentParser(description="Minimal serial probe for Proteus COMPIM link.")
    parser.add_argument("--port", default="COM2", help="PC-side virtual COM port, default COM2")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate, default 115200")
    parser.add_argument("--cmd", default="STAT?", help="Command to send, default STAT?")
    parser.add_argument("--seconds", type=float, default=5.0, help="Read duration after TX")
    args = parser.parse_args()

    frame = build_cmd_frame(args.cmd)
    print(f"OPEN port={args.port} baud={args.baud}")
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
        written = ser.write(frame)
        ser.flush()
        print(f"TX {frame!r} written={written}")
        print(f"LINES: CTS={int(ser.cts)} DSR={int(ser.dsr)} RTS={int(ser.rts)} DTR={int(ser.dtr)}")

        deadline = time.time() + args.seconds
        total = 0
        while time.time() < deadline:
            data = ser.read_until(b"\n")
            if data:
                total += len(data)
                print(f"RX {data!r}")
        print(f"DONE rx_bytes={total}")


if __name__ == "__main__":
    main()
