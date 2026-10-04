#!/usr/bin/env python3
"""Bridge a Concept2 PM5 (Bluetooth) to a standard Bluetooth FTMS rower.

Connects to the PM5 as a central (bleak), decodes its rowing notifications,
and re-advertises the data as an FTMS Rower (service 0x1826) peripheral (bless)
so apps like Peloton can pair with it.

Usage:
    python pm5_ftms.py                 # scan for a PM5 and bridge it
    python pm5_ftms.py --address XX    # connect to a specific PM5
    python pm5_ftms.py --simulate      # fake rowing data (no PM5 needed)
"""
import argparse
import asyncio
import logging
import math
import struct
import time
from dataclasses import dataclass

from bleak import BleakClient, BleakScanner
from bless import (
    BlessServer,
    GATTAttributePermissions,
    GATTCharacteristicProperties,
)

log = logging.getLogger("pm5_ftms")

# ---- Concept2 PM5 UUIDs ----------------------------------------------------
def c2(short: str) -> str:
    return f"ce06{short}-43e5-11e4-916c-0800200c9a66"

PM5_ROWING_SERVICE = c2("0030")
PM5_GENERAL_STATUS = c2("0031")
PM5_ADDITIONAL_STATUS = c2("0032")
PM5_ADDITIONAL_STATUS2 = c2("0033")
PM5_SAMPLE_RATE = c2("0034")
PM5_STROKE_DATA = c2("0035")
PM5_ADDITIONAL_STROKE_DATA = c2("0036")

# ---- FTMS UUIDs -------------------------------------------------------------
def sig(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"

FTMS_SERVICE = sig(0x1826)
FTMS_FEATURE = sig(0x2ACC)
FTMS_ROWER_DATA = sig(0x2AD1)
FTMS_CONTROL_POINT = sig(0x2AD9)
FTMS_STATUS = sig(0x2ADA)


@dataclass
class RowState:
    elapsed_s: float = 0.0
    distance_m: float = 0.0
    stroke_rate_spm: int = 0
    stroke_count: int = 0
    pace_s_per_500: float = 0.0
    avg_pace_s_per_500: float = 0.0
    power_w: int = 0
    avg_power_w: int = 0
    calories: int = 0
    heart_rate: int = 0  # 0 = unknown
    last_update: float = 0.0


def u24(b: bytes, i: int) -> int:
    return b[i] | (b[i + 1] << 8) | (b[i + 2] << 16)


def u16(b: bytes, i: int) -> int:
    return b[i] | (b[i + 1] << 8)


# ---- PM5 decoding -----------------------------------------------------------
class PM5Decoder:
    def __init__(self, state: RowState):
        self.s = state

    def general_status(self, _, d: bytearray):
        if len(d) < 6:
            return
        self.s.elapsed_s = u24(d, 0) / 100.0
        self.s.distance_m = u24(d, 3) / 10.0
        self._touch()

    def additional_status(self, _, d: bytearray):
        if len(d) < 11:
            return
        self.s.stroke_rate_spm = d[5]
        hr = d[6]
        self.s.heart_rate = 0 if hr == 255 else hr
        self.s.pace_s_per_500 = u16(d, 7) / 100.0
        self.s.avg_pace_s_per_500 = u16(d, 9) / 100.0
        self._touch()

    def additional_status2(self, _, d: bytearray):
        if len(d) < 8:
            return
        self.s.avg_power_w = u16(d, 4)
        self.s.calories = u16(d, 6)
        self._touch()

    def stroke_data(self, _, d: bytearray):
        if len(d) < 20:
            return
        self.s.stroke_count = u16(d, 18)
        self._touch()

    def additional_stroke_data(self, _, d: bytearray):
        if len(d) < 9:
            return
        self.s.power_w = u16(d, 3)
        self.s.stroke_count = u16(d, 7)
        self._touch()

    def _touch(self):
        self.s.last_update = time.monotonic()


async def run_pm5(state: RowState, address: str | None):
    dec = PM5Decoder(state)
    while True:
        try:
            if address:
                device = address
            else:
                log.info("Scanning for PM5 (wake the monitor / open Connect menu)...")
                dev = await BleakScanner.find_device_by_filter(
                    lambda d, ad: (d.name or "").startswith("PM5")
                    or PM5_ROWING_SERVICE in [u.lower() for u in ad.service_uuids],
                    timeout=30,
                )
                if not dev:
                    continue
                device = dev
                log.info("Found %s (%s)", dev.name, dev.address)

            async with BleakClient(device) as client:
                log.info("Connected to PM5")
                # Fastest sample rate: 3 = 100ms
                try:
                    await client.write_gatt_char(PM5_SAMPLE_RATE, bytes([3]), response=True)
                except Exception as e:
                    log.debug("Could not set sample rate: %s", e)
                for uuid, cb in [
                    (PM5_GENERAL_STATUS, dec.general_status),
                    (PM5_ADDITIONAL_STATUS, dec.additional_status),
                    (PM5_ADDITIONAL_STATUS2, dec.additional_status2),
                    (PM5_STROKE_DATA, dec.stroke_data),
                    (PM5_ADDITIONAL_STROKE_DATA, dec.additional_stroke_data),
                ]:
                    await client.start_notify(uuid, cb)
                while client.is_connected:
                    await asyncio.sleep(1)
                log.warning("PM5 disconnected")
        except Exception as e:
            log.warning("PM5 error: %s; retrying", e)
        await asyncio.sleep(2)


async def run_simulator(state: RowState):
    log.info("Simulating rowing data")
    start = time.monotonic()
    while True:
        t = time.monotonic() - start
        state.elapsed_s = t
        state.stroke_rate_spm = 24
        state.power_w = int(150 + 30 * math.sin(t / 5))
        # Concept2 pace/power: watts = 2.80 / (pace_per_m)^3
        pace_per_m = (2.80 / state.power_w) ** (1 / 3)
        state.pace_s_per_500 = pace_per_m * 500
        state.avg_pace_s_per_500 = state.pace_s_per_500
        state.distance_m += 0.2 / pace_per_m
        state.stroke_count = int(t * 24 / 60)
        state.avg_power_w = 150
        state.calories = int(t * 0.2)
        state.last_update = time.monotonic()
        await asyncio.sleep(0.2)


# ---- FTMS encoding ----------------------------------------------------------
# Fitness Machine Features: total distance(2), pace(5), expended energy(9),
# heart rate(10), elapsed time(12), power measurement(14). Target settings: none.
FTMS_FEATURE_VALUE = struct.pack(
    "<II", (1 << 2) | (1 << 5) | (1 << 9) | (1 << 10) | (1 << 12) | (1 << 14), 0
)


def encode_rower_data(s: RowState) -> bytes:
    # bit0 = 0 -> stroke rate + stroke count present
    flags = (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 11)
    if s.heart_rate:
        flags |= 1 << 9
    out = struct.pack("<H", flags)
    out += struct.pack("<BH", min(255, s.stroke_rate_spm * 2), s.stroke_count & 0xFFFF)
    dist = int(s.distance_m)
    out += bytes([dist & 0xFF, (dist >> 8) & 0xFF, (dist >> 16) & 0xFF])
    out += struct.pack("<H", min(0xFFFF, int(s.pace_s_per_500)))
    out += struct.pack("<hh", s.power_w, s.avg_power_w)
    # total energy (kcal), per hour (0xFFFF = n/a), per minute (0xFF = n/a)
    out += struct.pack("<HHB", s.calories, 0xFFFF, 0xFF)
    if s.heart_rate:
        out += struct.pack("<B", s.heart_rate)
    out += struct.pack("<H", min(0xFFFF, int(s.elapsed_s)))
    return out


async def run_ftms(state: RowState, name: str):
    loop = asyncio.get_running_loop()
    server = BlessServer(name=name, loop=loop)

    def on_read(characteristic, **kwargs):
        return characteristic.value

    def on_write(characteristic, value, **kwargs):
        characteristic.value = value
        if str(characteristic.uuid).lower() == FTMS_CONTROL_POINT and value:
            op = value[0]
            log.info("FTMS control point op 0x%02x", op)
            # Response: 0x80, request opcode, result 0x01 (success)
            characteristic.value = bytes([0x80, op, 0x01])
            server.update_value(FTMS_SERVICE, FTMS_CONTROL_POINT)

    server.read_request_func = on_read
    server.write_request_func = on_write

    P = GATTCharacteristicProperties
    A = GATTAttributePermissions
    await server.add_new_service(FTMS_SERVICE)
    await server.add_new_characteristic(
        FTMS_SERVICE, FTMS_FEATURE, P.read, bytearray(FTMS_FEATURE_VALUE), A.readable
    )
    await server.add_new_characteristic(
        FTMS_SERVICE, FTMS_ROWER_DATA, P.notify, None, A.readable
    )
    await server.add_new_characteristic(
        FTMS_SERVICE, FTMS_CONTROL_POINT, P.write | P.indicate, None,
        A.readable | A.writeable,
    )
    await server.add_new_characteristic(
        FTMS_SERVICE, FTMS_STATUS, P.notify, None, A.readable
    )
    await server.start(prioritize_local_name=False)
    log.info("Advertising FTMS rower as '%s'", name)

    last_log = 0.0
    while True:
        await asyncio.sleep(0.25)
        payload = encode_rower_data(state)
        server.get_characteristic(FTMS_ROWER_DATA).value = bytearray(payload)
        server.update_value(FTMS_SERVICE, FTMS_ROWER_DATA)
        if time.monotonic() - last_log > 5:
            last_log = time.monotonic()
            log.info(
                "t=%ds dist=%dm spm=%d strokes=%d pace=%.1fs/500 power=%dW hr=%d",
                state.elapsed_s, state.distance_m, state.stroke_rate_spm,
                state.stroke_count, state.pace_s_per_500, state.power_w, state.heart_rate,
            )


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--address", help="PM5 BLE address (Linux) or UUID (macOS)")
    ap.add_argument("--name", default="PM5 Row", help="advertised FTMS name")
    ap.add_argument("--simulate", action="store_true", help="generate fake data")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    state = RowState()
    source = run_simulator(state) if args.simulate else run_pm5(state, args.address)
    await asyncio.gather(source, run_ftms(state, args.name))


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
