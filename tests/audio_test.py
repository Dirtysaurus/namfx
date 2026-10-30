#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Peter Hudak
"""Sound card choice (System -> Audio input) against a fake /proc/asound.

Starts build/namfx with --null-audio and NAMFX_ASOUND_ROOT pointing to a fake tree (internal codec,
GO Twin, ALSA loopback), then drives the WebSocket API. Needs `pip install websocket-client`.
    python3 tests/audio_test.py
"""
import json, os, shutil, socket, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path
import websocket

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "namfx"

CARDS = {
    0: (" 0 [rockchiprk817co]: simple-card - rockchip,rk817-codec\n                      rockchip,rk817-codec\n", None),
    1: (" 1 [Twin           ]: USB-Audio - GO Twin\n                      TC Helicon GO Twin at usb-ff300000.usb-1, full speed\n", "0644:8010"),
    2: (" 2 [Loopback       ]: Loopback - Loopback\n                      Loopback 1\n", None),
}


def make_asound(root: Path, with_usb=True):
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    keep = [0, 2] + ([1] if with_usb else [])
    (root / "cards").write_text("".join(CARDS[i][0] for i in sorted(keep)))
    for i in keep:
        d = root / f"card{i}"
        (d / "pcm0c").mkdir(parents=True)
        (d / "pcm0p").mkdir()
        if CARDS[i][1]:
            (d / "usbid").write_text(CARDS[i][1] + "\n")


class Run:
    def __init__(self, tmp: Path, extra=()):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        self.port = s.getsockname()[1]
        s.close()
        self.data = tmp / "data"
        if not self.data.exists():
            shutil.copytree(ROOT / "data", self.data)
        env = dict(os.environ, NAMFX_ASOUND_ROOT=str(tmp / "asound"))
        self.p = subprocess.Popen([str(EXE), "--null-audio", "--no-display", "--no-lv2", "--no-hotspot", "--data", str(self.data),
                                   "--port", str(self.port), *extra], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
        self.ws = None
        for _ in range(100):
            try:
                self.ws = websocket.create_connection(f"ws://127.0.0.1:{self.port}/ws", timeout=5)
                break
            except OSError:
                time.sleep(0.1)
        assert self.ws, "namfx did not start"
        self.rid = 0

    def cmd(self, **c):
        self.rid += 1
        self.ws.send(json.dumps({**c, "rid": self.rid}))
        while True:
            m = json.loads(self.ws.recv())
            if m.get("rid") == self.rid:
                return m

    def audio(self):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/state", timeout=5) as r:
            return json.load(r)["audio_dev"]

    def wait(self, pred, what, timeout=8):
        end = time.time() + timeout
        while time.time() < end:
            a = self.audio()
            if pred(a):
                return a
            time.sleep(0.1)
        raise AssertionError(f"timeout: {what}; last state: {json.dumps(self.audio())}")

    def stop(self):
        try:
            self.ws.close()
        except Exception:
            pass
        self.p.terminate()
        try:
            self.p.wait(5)
        except subprocess.TimeoutExpired:
            self.p.kill()


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        sys.exit(1)


def main():
    tmp = Path(tempfile.mkdtemp(prefix="namfx-audio-"))
    asound = tmp / "asound"
    make_asound(asound, with_usb=True)
    r = Run(tmp)
    try:
        a = r.audio()
        check(a["active"]["device"] == "card:Twin" and a["active"]["label"] == "GO Twin", "auto picks the USB interface")
        check(a["active"]["capture"] == "plughw:CARD=Twin,DEV=0" and a["active"]["playback"] == a["active"]["capture"],
              "USB uses plughw for input and output")
        check(a["active"]["channel"] == 0, "USB default channel is left")
        check([c["id"] for c in a["cards"]] == ["rockchiprk817co", "Twin", "Loopback"], "all three cards are listed")
        check(a["cards"][1]["usb"] and a["cards"][1]["usbid"] == "0644:8010" and not a["cards"][0]["usb"], "USB flag and id")
        check(not a["manual"], "not manual")

        check(r.cmd(cmd="audio_set", device="internal")["ok"], "audio_set internal accepted")
        a = r.wait(lambda a: a["active"]["device"] == "internal", "internal chosen")
        check(a["active"]["capture"] == "hw:0,0" and a["active"]["channel"] == 1,
              "internal is hw:0,0 on the right channel (not the loopback card)")
        check(json.loads((r.data / "settings.json").read_text())["audio"]["device"] == "internal", "choice is saved to settings.json")

        check(r.cmd(cmd="audio_set", channel="sum")["ok"], "audio_set channel sum accepted")
        r.wait(lambda a: a["active"]["channel"] == 2, "sum channel")
        r.cmd(cmd="audio_set", channel="auto", device="card:Twin")
        r.wait(lambda a: a["active"]["device"] == "card:Twin" and a["active"]["channel"] == 0, "back to the USB card with auto channel")

        m = r.cmd(cmd="audio_set", device="../etc")
        check(not m["ok"] and m["code"] == "err.audio_device", "bad device is refused")
        m = r.cmd(cmd="audio_set", channel="middle")
        check(not m["ok"] and m["code"] == "err.audio_channel", "bad channel is refused")
        check(r.audio()["choice"]["device"] == "card:Twin", "a refused command changes nothing")

        make_asound(asound, with_usb=False)  # unplugged
        check(r.cmd(cmd="audio_scan")["ok"], "audio_scan accepted")
        a = r.audio()
        check([c["id"] for c in a["cards"]] == ["rockchiprk817co", "Loopback"], "the unplugged card disappears from the list")
        r.cmd(cmd="audio_set", device="card:Twin")  # the chosen card is gone: apply falls back
        a = r.wait(lambda a: a["active"].get("missing") == "Twin", "missing card reported")
        check(a["active"]["device"] == "internal" and a["active"]["channel"] == 1, "missing card falls back to the internal input")

        make_asound(asound, with_usb=True)  # plugged in again, the choice is still card:Twin
        r.cmd(cmd="audio_set", device="card:Twin")
        r.wait(lambda a: a["active"]["device"] == "card:Twin" and "missing" not in a["active"], "plugged in again")
    finally:
        r.stop()

    make_asound(asound, with_usb=False)
    shutil.rmtree(tmp / "data")  # no saved choice
    r = Run(tmp)
    try:
        a = r.audio()
        check(a["active"]["device"] == "internal" and a["active"]["channel"] == 1,
              "without USB the internal input on the right channel is the default")
    finally:
        r.stop()

    r = Run(tmp, extra=["--capture", "hw:9,9", "--in-channel", "2"])
    try:
        a = r.audio()
        check(a["manual"] and a["active"]["capture"] == "hw:9,9", "--capture / --in-channel win and are reported as manual")
    finally:
        r.stop()
    shutil.rmtree(tmp, ignore_errors=True)
    print("all good")


if __name__ == "__main__":
    main()
