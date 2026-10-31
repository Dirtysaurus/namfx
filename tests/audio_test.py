#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Peter Hudak
"""Sound card choice (System -> Audio input) against a fake /proc/asound.

Starts build/namfx with --null-audio and NAMFX_ASOUND_ROOT pointing to a fake tree (internal codec,
GO Twin, ALSA loopback), then drives the WebSocket API. Needs `pip install websocket-client`.
    python3 tests/audio_test.py
"""
import json, os, shutil, socket, subprocess, sys, tempfile, time, urllib.error, urllib.request
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
    def __init__(self, tmp: Path, extra=(), connect=True):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        self.port = s.getsockname()[1]
        s.close()
        self.data = tmp / "data"
        if not self.data.exists():
            shutil.copytree(ROOT / "data", self.data)
        env = dict(os.environ, NAMFX_ASOUND_ROOT=str(tmp / "asound"), NAMFX_ASOUND_CONF=str(tmp / "asound.conf"))
        self.p = subprocess.Popen([str(EXE), "--null-audio", "--no-display", "--no-lv2", "--no-hotspot", "--data", str(self.data),
                                   "--port", str(self.port), *extra], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
        self.ws = None
        self.rid = 0
        for _ in range(100):
            try:
                if connect:
                    self.ws = websocket.create_connection(f"ws://127.0.0.1:{self.port}/ws", timeout=5)
                else:
                    socket.create_connection(("127.0.0.1", self.port), timeout=1).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("namfx did not start")

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
        pw = json.loads((r.data / "settings.json").read_text())["hotspot"]["password"]
        check(len(pw) == 10 and pw != "namfx1234", "a random hotspot password is generated on first start")

        check(r.cmd(cmd="audio_set", device="internal")["ok"], "audio_set internal accepted")
        a = r.wait(lambda a: a["active"]["device"] == "internal", "internal chosen")
        check(a["active"]["capture"] == "hw:0,0" and a["active"]["channel"] == 1,
              "internal is hw:0,0 on the right channel (not the loopback card)")
        check(json.loads((r.data / "settings.json").read_text())["audio"]["device"] == "internal", "choice is saved to settings.json")

        check(r.cmd(cmd="audio_set", channel="sum")["ok"], "audio_set channel sum accepted")
        r.wait(lambda a: a["active"]["channel"] == 2, "sum channel")
        r.cmd(cmd="audio_set", channel="auto", device="card:Twin")
        r.wait(lambda a: a["active"]["device"] == "card:Twin" and a["active"]["channel"] == 0, "back to the USB card with auto channel")

        (tmp / "asound.conf").unlink(missing_ok=True)
        check(r.cmd(cmd="audio_set", output="bluetooth")["ok"], "output bluetooth accepted")
        a = r.wait(lambda a: a["choice"]["output"] == "bluetooth", "bluetooth chosen")
        check(a["active"]["playback"] == a["active"]["capture"] and not a["active"]["bt_route"],
              "no Bluetooth route (no Loopback in asound.conf): playing on the card")
        (tmp / "asound.conf").write_text("pcm.!default {
 type plug
 slave.pcm \"hw:Loopback,0,0\"
}
")  # what the bt-vol service writes
        a = r.wait(lambda a: a["active"]["playback"] == "default" and a["active"]["bt_route"], "headphones connected -> default", timeout=10)
        check(a["active"]["capture"].startswith("plughw:"), "input stays on the card")
        (tmp / "asound.conf").unlink()
        r.wait(lambda a: a["active"]["playback"] == a["active"]["capture"] and not a["active"]["bt_route"], "headphones gone -> back to the card", timeout=10)
        check(r.cmd(cmd="audio_set", output="pcm:plughw:CARD=Loopback,DEV=0")["ok"], "explicit pcm accepted")
        a = r.wait(lambda a: a["active"]["playback"] == "plughw:CARD=Loopback,DEV=0", "custom output")
        m = r.cmd(cmd="audio_set", output="pcm:x;rm")
        check(not m["ok"] and m["code"] == "err.audio_output", "bad output name is refused")
        r.cmd(cmd="audio_set", output="same")
        r.wait(lambda a: a["active"]["playback"] == a["active"]["capture"], "output back to the input card")
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
        check(r.cmd(cmd="mute", on=True)["ok"], "mute accepted")
        with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
            check(json.load(resp)["muted"] is True, "state reports muted")
        def chain_len():
            with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
                return len(json.load(resp)["chain"])
        r.cmd(cmd="preset_new")
        check(chain_len() == 0, "new preset is empty")
        check(not r.cmd(cmd="undo")["ok"], "nothing to undo in a fresh preset")
        check(r.cmd(cmd="add_block", type="volume")["ok"] and chain_len() == 1, "block added")
        check(r.cmd(cmd="undo")["ok"] and chain_len() == 0, "undo removes the added block")
        check(r.cmd(cmd="redo")["ok"] and chain_len() == 1, "redo puts it back")
        check(r.cmd(cmd="metronome", on=True, bpm=999, volume=-20)["ok"], "metronome accepted")
        with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
            me = json.load(resp)["metronome"]
        check(me["on"] is True and me["bpm"] == 300 and me["volume"] == -20, "metronome state, bpm clamped to 300")
        r.cmd(cmd="metronome", on=False)
        check(r.cmd(cmd="gain_wizard", on=True)["ok"], "level wizard started")
        end = time.time() + 15
        res = ""
        while time.time() < end and not res:
            m = json.loads(r.ws.recv())
            if m.get("t") == "status":
                res = m.get("wizard", {}).get("result", "")
        check(res == "nosignal", "no input signal -> wizard reports nosignal and keeps the gain")
        def looper():
            with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
                return json.load(resp)["looper"]
        check(looper()["state"] == "empty", "looper starts empty")
        r.cmd(cmd="looper", action="rec")
        time.sleep(0.6)
        check(looper()["state"] == "rec" and looper()["seconds"] > 0.2, "looper records")
        r.cmd(cmd="looper", action="rec")
        time.sleep(0.2)
        lp = looper()
        check(lp["state"] == "play" and lp["seconds"] > 0.2, "second press closes the loop and plays")
        r.cmd(cmd="looper", action="overdub")
        time.sleep(0.2)
        check(looper()["state"] == "overdub", "overdub")
        r.cmd(cmd="looper", action="clear")
        time.sleep(0.2)
        check(looper()["state"] == "empty", "clear")
        check(not r.cmd(cmd="looper", action="bogus")["ok"], "unknown looper action refused")
        r.cmd(cmd="mute")  # toggle back
        with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
            check(json.load(resp)["muted"] is False, "mute toggles back")
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

    # optional web password: settings.json "web_password" -> HTTP Basic auth (user namfx)
    (tmp / "data" / "settings.json").write_text(json.dumps({"web_password": "s3cret"}))
    r = Run(tmp, connect=False)
    try:
        import base64
        url = f"http://127.0.0.1:{r.port}/api/state"
        try:
            urllib.request.urlopen(url, timeout=5)
            check(False, "a password-protected server refuses requests without credentials")
        except urllib.error.HTTPError as e:
            check(e.code == 401, "no credentials -> 401")
        bad = urllib.request.Request(url, headers={"Authorization": "Basic " + base64.b64encode(b"namfx:wrong").decode()})
        try:
            urllib.request.urlopen(bad, timeout=5)
            check(False, "wrong password is refused")
        except urllib.error.HTTPError as e:
            check(e.code == 401, "wrong password -> 401")
        good = urllib.request.Request(url, headers={"Authorization": "Basic " + base64.b64encode(b"namfx:s3cret").decode()})
        check(urllib.request.urlopen(good, timeout=5).status == 200, "right password -> 200")
    finally:
        r.stop()
    shutil.rmtree(tmp, ignore_errors=True)
    print("all good")


if __name__ == "__main__":
    main()
