#!/usr/bin/env python3
"""Push local Kaboo / Claude usage to the AI Passport over BLE.

Reads two local files, packs them into the fixed WIRE_SIZE-byte wire format that
main/usage_model.c decodes, and writes it to the device's GATT characteristic.

Data sources
------------
Claude quota comes from /tmp/flux-rl.json, written by the flux statusline hook
on every Claude Code invocation. This is the authoritative source: kaboo's own
`plans[0]` has been observed without a `quotas` key at all
(`status: "not_configured"`), so it is only consulted as a fallback and never
indexed blindly.

Kaboo token counts come from the menubar snapshot, refreshed every 5 minutes by
the kaboo menubar agent. Note `top_model` is an object, not a string -- the
display name lives in `top_model.display`.

Usage
-----
    python3 tools/usage_bridge.py --once     # single push, then exit
    python3 tools/usage_bridge.py            # push every 60 s
    python3 tools/usage_bridge.py --dry-run  # pack and print, no BLE

Requires bleak (pip install bleak).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import pathlib
import struct
import sys
import time

FLUX_RL = pathlib.Path("/tmp/flux-rl.json")
CLAUDE_SNAPSHOT = pathlib.Path.home() / ".claude/statusline-snapshot.json"
KABOO_SNAPSHOT = (
    pathlib.Path.home() / ".local/share/kaboo/menubar_last_good_snapshot.json"
)

DEVICE_NAME = "FoloPassport"
SVC_UUID = "6b1d0001-5f9a-4c33-9a1e-2f8b7c4d5e60"
CHR_UUID = "6b1d0002-5f9a-4c33-9a1e-2f8b7c4d5e60"

# Must match USAGE_WIRE_SIZE and the offsets in main/usage_model.c. The host
# test in tests/test_usage_model.c pins the same layout from the C side.
# v2 adds a 30-day window (tokens + cents) after the 7-day one.
WIRE_FORMAT = "<BB I h I I I Q Q Q I I I I 24s BB I I"
WIRE_SIZE = 94
WIRE_VERSION = 2

FLAG_KABOO_VALID = 1 << 0
FLAG_FIVE_HOUR = 1 << 1
FLAG_SEVEN_DAY = 1 << 2

TOP_MODEL_CAP = 24


def local_utc_offset_minutes() -> int:
    """Current UTC offset in minutes, DST included for *this* moment."""
    if time.localtime().tm_isdst and time.daylight:
        return -time.altzone // 60
    return -time.timezone // 60


def _iso_to_unix(value: object) -> int:
    """Parse an ISO-8601 timestamp; 0 when absent or malformed."""
    if not isinstance(value, str):
        return 0
    try:
        import datetime

        return int(
            datetime.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
        )
    except ValueError:
        return 0


def _window_flags(five: dict, seven: dict, five_reset: int, seven_reset: int) -> int:
    """Flag only the windows that carry BOTH a percent and a usable reset epoch.

    The firmware runs epoch_plausible() on the reset time of every flagged
    window and rejects the whole packet if one fails, so flagging a window
    whose reset we could not parse would discard the other window and the
    Kaboo data along with it. EPOCH_MIN mirrors USAGE_EPOCH_MIN in
    main/usage_model.h (2020-01-01).
    """
    EPOCH_MIN = 1577836800
    flags = 0
    if five and five_reset >= EPOCH_MIN:
        flags |= FLAG_FIVE_HOUR
    if seven and seven_reset >= EPOCH_MIN:
        flags |= FLAG_SEVEN_DAY
    return flags


def read_claude() -> tuple[int, int, int, int, int, int]:
    """Return (window_flags, sampled_unix, five_pct, seven_pct, five_reset, seven_reset).

    window_flags carries FLAG_FIVE_HOUR / FLAG_SEVEN_DAY for the windows this
    source actually provided. Claude Code only emits a window while it is
    active -- a payload with just `seven_day` is normal -- so the two are
    reported independently. Signalling a window we do not have would make the
    firmware reject the whole packet on its zero reset epoch, taking valid
    Kaboo data down with it.

    Source order, most authoritative first:

    1. ~/.claude/statusline-snapshot.json -- written by kaboo's own statusline
       hook. This is the canonical form: percentages plus ISO reset times.
    2. kaboo's menubar snapshot `plans[]` -- same data, aggregated. Often
       absent: `status: "not_configured"` with no `quotas` key at all.
    3. /tmp/flux-rl.json -- raw `rate_limits` cached by the third-party flux
       statusline script. Last resort, since it depends on flux staying
       installed and on its private file format.

    Every level is optional and every shape is guarded; a missing or malformed
    source falls through rather than raising.
    """
    # 1. kaboo statusline snapshot.
    try:
        raw = json.loads(CLAUDE_SNAPSHOT.read_text())
        quotas = raw.get("quotas")
        if isinstance(quotas, dict):
            five = quotas.get("five_hour") or {}
            seven = quotas.get("seven_day") or {}
            if five or seven:
                sampled = _iso_to_unix(raw.get("updated_at"))
                if sampled == 0:
                    sampled = int(CLAUDE_SNAPSHOT.stat().st_mtime)
                five_reset = _iso_to_unix(five.get("resets_at"))
                seven_reset = _iso_to_unix(seven.get("resets_at"))
                flags = _window_flags(five, seven, five_reset, seven_reset)
                if flags:
                    return (
                        flags,
                        sampled,
                        int(five.get("used_percent", 0)),
                        int(seven.get("used_percent", 0)),
                        five_reset,
                        seven_reset,
                    )
    except (json.JSONDecodeError, TypeError, ValueError, OSError) as exc:
        print(f"note: {CLAUDE_SNAPSHOT.name} unusable ({exc})", file=sys.stderr)

    # 2. kaboo menubar plans. Never index blindly -- `quotas` is often missing.
    try:
        snap = json.loads(KABOO_SNAPSHOT.read_text())["snapshot"]
        for plan in snap.get("plans", []):
            if not isinstance(plan, dict):
                continue
            quotas = plan.get("quotas")
            if not isinstance(quotas, list):
                continue
            by_key = {q.get("key"): q for q in quotas if isinstance(q, dict)}
            five = by_key.get("five_hour") or {}
            seven = by_key.get("seven_day") or {}
            if not five and not seven:
                continue
            sampled = _iso_to_unix(plan.get("fetched_at")) or int(time.time())
            five_reset = _iso_to_unix(five.get("resets_at"))
            seven_reset = _iso_to_unix(seven.get("resets_at"))
            flags = _window_flags(five, seven, five_reset, seven_reset)
            if not flags:
                continue
            return (
                flags,
                sampled,
                int(five.get("used_percent", 0)),
                int(seven.get("used_percent", 0)),
                five_reset,
                seven_reset,
            )
    except (json.JSONDecodeError, KeyError, TypeError, ValueError, OSError) as exc:
        print(f"note: kaboo plans unusable ({exc})", file=sys.stderr)

    # 3. flux cache. Claude Code emits a window only while it is active, so
    #    seven_day may appear alone; treat the two independently.
    try:
        raw = json.loads(FLUX_RL.read_text())
        five = raw.get("five_hour") or {}
        seven = raw.get("seven_day") or {}
        if five or seven:
            five_reset = int(five.get("resets_at", 0))
            seven_reset = int(seven.get("resets_at", 0))
            flags = _window_flags(five, seven, five_reset, seven_reset)
            if flags:
                return (
                    flags,
                    int(FLUX_RL.stat().st_mtime),
                    int(five.get("used_percentage", 0)),
                    int(seven.get("used_percentage", 0)),
                    five_reset,
                    seven_reset,
                )
    except (json.JSONDecodeError, TypeError, ValueError, OSError) as exc:
        print(f"note: flux cache unusable ({exc})", file=sys.stderr)

    print("warning: no Claude quota source available", file=sys.stderr)
    return (0, 0, 0, 0, 0, 0)


def read_kaboo() -> tuple[bool, int, int, int, int, int, int, int, int, int, str]:
    """Return (valid, sampled_unix,
               today/week/month/all tokens, today/week/month/all cents, model).

    The 30-day window is not in `tokens`; it lives under
    `analytics.periods.month.stats` (range "30D").
    """
    try:
        snap = json.loads(KABOO_SNAPSHOT.read_text())["snapshot"]
        tokens = snap["tokens"]
        month = (
            snap.get("analytics", {}).get("periods", {}).get("month", {}).get("stats", {})
        )
        if not isinstance(month, dict):
            month = {}

        sampled = int(time.time())
        generated = snap.get("generated_at")
        if isinstance(generated, str):
            try:
                import datetime

                sampled = int(
                    datetime.datetime.fromisoformat(generated).timestamp()
                )
            except ValueError:
                pass

        def count(section: str) -> int:
            return int(tokens.get(section, {}).get("tokens", 0))

        def cents(section: str) -> int:
            return int(round(float(tokens.get(section, {}).get("cost_usd", 0.0)) * 100))

        # top_model is an object; the human-readable name is under .display.
        model_obj = snap.get("top_model")
        model = ""
        if isinstance(model_obj, dict):
            model = str(model_obj.get("display") or model_obj.get("model") or "")
        elif isinstance(model_obj, str):
            model = model_obj

        return (
            True,
            sampled,
            count("today"),
            count("seven_day"),
            int(month.get("tokens", 0)),
            count("all_time"),
            cents("today"),
            cents("seven_day"),
            int(round(float(month.get("cost_usd", 0.0)) * 100)),
            cents("all_time"),
            model,
        )
    except (json.JSONDecodeError, KeyError, TypeError, ValueError, OSError) as exc:
        print(f"warning: could not read kaboo tokens: {exc}", file=sys.stderr)
        return (False, 0, 0, 0, 0, 0, 0, 0, 0, 0, "")


def build_payload() -> bytes:
    """Pack the current readings. generated_unix is stamped by the caller."""
    (k_ok, k_sampled, today, week, month, all_time,
     today_c, week_c, month_c, all_c, model) = read_kaboo()
    c_flags, c_sampled, five, seven, five_reset, seven_reset = read_claude()

    flags = c_flags          # per-window Claude bits, already validated
    if k_ok:
        flags |= FLAG_KABOO_VALID

    # Leave room for the NUL the firmware forces at index 23 regardless.
    model_bytes = model.encode("utf-8", "replace")[: TOP_MODEL_CAP - 1]

    # Stamped as late as possible so scan/connect latency does not become
    # clock error on the device.
    generated = int(time.time())

    # uint32 fields are clamped; a machine with >4G today-tokens would wrap.
    payload = struct.pack(
        WIRE_FORMAT,
        WIRE_VERSION,
        flags,
        generated,
        local_utc_offset_minutes(),
        k_sampled,
        c_sampled,
        min(today, 0xFFFFFFFF),
        week,
        month,
        all_time,
        min(today_c, 0xFFFFFFFF),
        min(week_c, 0xFFFFFFFF),
        min(month_c, 0xFFFFFFFF),
        min(all_c, 0xFFFFFFFF),
        model_bytes,
        min(five, 100),
        min(seven, 100),
        five_reset,
        seven_reset,
    )
    assert len(payload) == WIRE_SIZE, f"payload is {len(payload)}B, expected {WIRE_SIZE}"
    return payload


def describe(payload: bytes) -> str:
    fields = struct.unpack(WIRE_FORMAT, payload)
    model = fields[14].split(b"\x00")[0].decode("utf-8", "replace")
    return (
        f"version={fields[0]} flags=0b{fields[1]:02b} generated={fields[2]} "
        f"tz={fields[3]}min\n"
        f"  kaboo: today={fields[6]} week={fields[7]} month={fields[8]} "
        f"all={fields[9]} model={model!r}\n"
        f"  claude: 5h={fields[15]}% 7d={fields[16]}% "
        f"resets={fields[17]}/{fields[18]}"
    )


async def push(payload: bytes, timeout: float, address: str | None = None) -> bool:
    """Push one payload. Returns False on any recoverable failure.

    Never raises for an offline or flaky device: the daemon loop must survive a
    dropped connection and retry on the next tick, and a caller that exits on
    the first transient error would leave the display frozen on stale data
    until someone restarts it by hand.
    """
    try:
        from bleak import BleakClient, BleakScanner
        from bleak.exc import BleakError
    except ImportError:
        print(
            "bleak is not installed. Install it with:\n"
            "  uv pip install bleak",
            file=sys.stderr,
        )
        return False

    try:
        if address:
            # An explicit --device wins: the name is not an identity, and on a
            # desk with two Passports the wrong one would silently receive this
            # machine's usage.
            device = await BleakScanner.find_device_by_address(
                address, timeout=timeout
            )
            if device is None:
                print(f"device {address} not found", file=sys.stderr)
                return False
        else:
            # Match ONLY the service UUID. Matching the advertised name too
            # would also accept any other Passport, including one running old
            # firmware without this characteristic.
            device = await BleakScanner.find_device_by_filter(
                lambda d, ad: SVC_UUID.lower()
                in [u.lower() for u in (ad.service_uuids or [])],
                timeout=timeout,
            )
            if device is None:
                print(
                    "device not found (powered on? in range? "
                    "use --device <address> to pin one)",
                    file=sys.stderr,
                )
                return False

        async with BleakClient(device) as client:
            # response=True so low-MTU links use prepare/execute long write and
            # the firmware still receives all WIRE_SIZE bytes in one callback.
            await client.write_gatt_char(CHR_UUID, payload, response=True)
    except (BleakError, asyncio.TimeoutError, OSError) as exc:
        # Backends raise OSError for adapter-level trouble and BleakError for
        # connect/write failures; both are expected when a battery-powered
        # device wanders off.
        print(f"push failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        return False

    print(f"pushed {len(payload)} bytes to {device.address}")
    return True


async def run(args: argparse.Namespace) -> int:
    while True:
        # build_payload reads external JSON; a corrupt or half-written file must
        # not kill a long-running daemon either.
        try:
            payload = build_payload()
        except Exception as exc:                      # noqa: BLE001
            print(f"could not build payload: {type(exc).__name__}: {exc}",
                  file=sys.stderr)
            if args.once:
                return 1
            await asyncio.sleep(args.interval)
            continue

        if args.dry_run:
            print(describe(payload))
            print(f"  {len(payload)} bytes: {payload.hex()}")
            return 0

        ok = await push(payload, args.scan_timeout, args.device)
        if args.once:
            return 0 if ok else 1
        await asyncio.sleep(args.interval)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--once", action="store_true", help="push once and exit")
    parser.add_argument(
        "--dry-run", action="store_true", help="pack and print without touching BLE"
    )
    parser.add_argument(
        "--interval", type=float, default=60.0, help="seconds between pushes"
    )
    parser.add_argument(
        "--scan-timeout", type=float, default=10.0, help="BLE scan timeout"
    )
    parser.add_argument(
        "--device",
        metavar="ADDRESS",
        help="pin one device by BLE address (macOS: the CoreBluetooth UUID "
             "printed on a successful push). Without it the first device "
             "advertising the service UUID wins, which is ambiguous when two "
             "Passports are in range.",
    )
    args = parser.parse_args()

    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
