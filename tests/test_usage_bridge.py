#!/usr/bin/env python3
"""Host tests for the pure parts of tools/usage_bridge.py.

BLE itself is not exercised here (bleak is imported lazily inside push()); the
tests pin the decisions that decide what reaches the device: which Claude
windows get flagged, how malformed source JSON is tolerated, and how a scan
with zero, one, or several matching boards is resolved.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import usage_bridge as bridge

RESET = bridge.EPOCH_MIN + 86400


def _adv(uuids: list[str], rssi: int = -50) -> SimpleNamespace:
    return SimpleNamespace(service_uuids=uuids, rssi=rssi)


def _dev(address: str, name: str | None = "FoloPassport") -> SimpleNamespace:
    return SimpleNamespace(address=address, name=name)


class PercentTests(unittest.TestCase):
    def test_accepts_integers_and_numeric_strings_in_range(self) -> None:
        self.assertEqual(bridge._percent({"used_percent": 42}, "used_percent"), 42)
        self.assertEqual(bridge._percent({"used_percent": "7"}, "used_percent"), 7)
        self.assertEqual(bridge._percent({"used_percent": 0}, "used_percent"), 0)
        self.assertEqual(bridge._percent({"used_percent": 100}, "used_percent"), 100)

    def test_rejects_missing_null_garbage_and_out_of_range(self) -> None:
        for window in (
            {},
            {"used_percent": None},
            {"used_percent": "n/a"},
            {"used_percent": []},
            {"used_percent": 101},
            {"used_percent": -1},
            {"used_percent": True},            # bool is not a reading
            {"used_percent": float("inf")},
            {"used_percent": float("nan")},
            {"used_percent": "1e999"},
        ):
            self.assertIsNone(bridge._percent(window, "used_percent"), window)

    def test_rounds_finite_floats_and_numeric_strings(self) -> None:
        self.assertEqual(bridge._percent({"p": 12.5}, "p"), 12)
        self.assertEqual(bridge._percent({"p": "42.9"}, "p"), 43)
        self.assertEqual(bridge._percent({"p": 99.6}, "p"), 100)


class ResetEpochTests(unittest.TestCase):
    def test_reset_must_fit_uint32_and_be_a_number(self) -> None:
        self.assertEqual(bridge._int_or_zero(1788420000), 1788420000)
        self.assertEqual(bridge._int_or_zero("1788420000"), 1788420000)
        self.assertEqual(bridge._int_or_zero(True), 0)
        self.assertEqual(bridge._int_or_zero(float("inf")), 0)
        self.assertEqual(bridge._int_or_zero(2**32), 0)
        self.assertEqual(bridge._int_or_zero(-1), 0)
        self.assertEqual(bridge._int_or_zero(None), 0)


class WindowFlagTests(unittest.TestCase):
    def test_both_windows_flagged_when_both_complete(self) -> None:
        self.assertEqual(
            bridge._window_flags(10, 20, RESET, RESET),
            bridge.FLAG_FIVE_HOUR | bridge.FLAG_SEVEN_DAY,
        )

    def test_reset_without_percent_is_not_flagged(self) -> None:
        # Would otherwise render on the device as a fabricated 0%.
        self.assertEqual(
            bridge._window_flags(None, 20, RESET, RESET), bridge.FLAG_SEVEN_DAY
        )

    def test_percent_without_plausible_reset_is_not_flagged(self) -> None:
        # The firmware rejects the whole packet on an implausible reset epoch.
        self.assertEqual(bridge._window_flags(10, 20, 0, RESET), bridge.FLAG_SEVEN_DAY)
        self.assertEqual(
            bridge._window_flags(10, 20, RESET, bridge.EPOCH_MIN - 1),
            bridge.FLAG_FIVE_HOUR,
        )

    def test_nothing_usable_flags_nothing(self) -> None:
        self.assertEqual(bridge._window_flags(None, None, RESET, RESET), 0)
        self.assertEqual(bridge._window_flags(10, 20, 0, 0), 0)

    def test_reset_beyond_uint32_is_not_flagged(self) -> None:
        # It could not be packed into the wire's uint32 anyway.
        self.assertEqual(
            bridge._window_flags(10, 20, RESET, bridge.UINT32_MAX + 1),
            bridge.FLAG_FIVE_HOUR,
        )


class ClaudeSourceShapeTests(unittest.TestCase):
    """read_claude() must fall through malformed-but-valid JSON, never raise."""

    def _read(self, snapshot: object, kaboo: object, flux: object) -> tuple:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            paths = {
                "CLAUDE_SNAPSHOT": root / "snapshot.json",
                "KABOO_SNAPSHOT": root / "kaboo.json",
                "FLUX_RL": root / "flux.json",
            }
            for name, payload in (
                ("CLAUDE_SNAPSHOT", snapshot),
                ("KABOO_SNAPSHOT", kaboo),
                ("FLUX_RL", flux),
            ):
                paths[name].write_text(json.dumps(payload))
            with contextlib.ExitStack() as stack:
                for name, path in paths.items():
                    stack.enter_context(mock.patch.object(bridge, name, path))
                stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
                return bridge.read_claude()

    def test_list_shaped_sources_fall_through_to_flux(self) -> None:
        flags, _sampled, five, seven, five_reset, seven_reset = self._read(
            snapshot=[],                                   # root is a list
            kaboo={"snapshot": {"plans": [{"quotas": {"five_hour": []}}]}},
            flux={
                "five_hour": {"used_percentage": 13, "resets_at": RESET},
                "seven_day": {"used_percentage": 25, "resets_at": RESET + 60},
            },
        )
        self.assertEqual(flags, bridge.FLAG_FIVE_HOUR | bridge.FLAG_SEVEN_DAY)
        self.assertEqual((five, seven), (13, 25))
        self.assertEqual((five_reset, seven_reset), (RESET, RESET + 60))

    def test_list_in_place_of_window_dict_is_ignored(self) -> None:
        flags, _s, five, seven, *_ = self._read(
            snapshot={"quotas": {"five_hour": [1, 2], "seven_day": {
                "used_percent": 40, "resets_at": "2026-09-10T00:00:00Z"}}},
            kaboo={},
            flux={},
        )
        self.assertEqual(flags, bridge.FLAG_SEVEN_DAY)
        self.assertEqual((five, seven), (0, 40))

    def test_flux_reset_without_percentage_leaves_window_inactive(self) -> None:
        flags, _s, five, seven, *_ = self._read(
            snapshot={},
            kaboo={},
            flux={
                "five_hour": {"resets_at": RESET},          # no percentage
                "seven_day": {"used_percentage": 25, "resets_at": RESET},
            },
        )
        self.assertEqual(flags, bridge.FLAG_SEVEN_DAY)
        self.assertEqual((five, seven), (0, 25))

    def test_everything_unusable_reports_no_windows(self) -> None:
        flags, *_ = self._read(snapshot=[], kaboo=[], flux=[])
        self.assertEqual(flags, 0)


class DevicePickTests(unittest.TestCase):
    def _pick(self, found: dict) -> tuple[object | None, str]:
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            device = bridge._pick_unique(found)
        return device, err.getvalue()

    def test_no_advertiser_of_the_service_is_not_found(self) -> None:
        device, err = self._pick({
            "AA": (_dev("AA"), _adv(["0000180f-0000-1000-8000-00805f9b34fb"])),
        })
        self.assertIsNone(device)
        self.assertIn("not found", err)

    def test_single_match_is_returned_case_insensitively(self) -> None:
        device, _err = self._pick({
            "AA": (_dev("AA"), _adv([bridge.SVC_UUID.upper()])),
            "BB": (_dev("BB"), _adv([])),
        })
        self.assertIsNotNone(device)
        self.assertEqual(device.address, "AA")

    def test_two_matches_refuse_and_list_both(self) -> None:
        device, err = self._pick({
            "AA": (_dev("AA"), _adv([bridge.SVC_UUID], rssi=-40)),
            "BB": (_dev("BB", name=None), _adv([bridge.SVC_UUID], rssi=-70)),
        })
        self.assertIsNone(device)
        self.assertIn("--device", err)
        self.assertIn("AA", err)
        self.assertIn("BB", err)
        self.assertIn("rssi=-70", err)

    def test_old_bleak_list_shape_names_the_upgrade(self) -> None:
        # bleak < 0.19: discover() returns a list and knows no return_adv.
        device, err = self._pick([_dev("AA")])  # type: ignore[arg-type]
        self.assertIsNone(device)
        self.assertIn("bleak >= 0.19", err)


if __name__ == "__main__":
    unittest.main()
