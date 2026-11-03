#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Peter Hudak
"""Sound card choice (System -> Audio input) against a fake /proc/asound.

Starts build/namfx with --null-audio and NAMFX_ASOUND_ROOT pointing to a fake tree (internal codec,
GO Twin, ALSA loopback), then drives the WebSocket API. Needs `pip install websocket-client`.
    python3 tests/audio_test.py
"""
import json, os, shutil, socket, struct, subprocess, sys, tempfile, time, urllib.error, urllib.request
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
    def __init__(self, tmp: Path, extra=(), connect=True, env_extra=None):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        self.port = s.getsockname()[1]
        s.close()
        self.data = tmp / "data"
        if not self.data.exists():
            shutil.copytree(ROOT / "data", self.data)
        env = dict(os.environ, NAMFX_ASOUND_ROOT=str(tmp / "asound"), NAMFX_ASOUND_CONF=str(tmp / "asound.conf"), **(env_extra or {}))
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
        check(r.cmd(cmd="net_set", password="")["ok"], "an empty hotspot password (open network) is accepted")
        m = r.cmd(cmd="net_set", password="short")
        check(not m["ok"] and m["code"] == "err.net_password", "a 5 character password is refused")
        check(r.cmd(cmd="net_set", password="longenough1")["ok"], "an 8-63 character password is accepted")
        r.cmd(cmd="net_set", password="")

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
        (tmp / "asound.conf").write_text('pcm.!default { type plug slave.pcm "hw:Loopback,0,0" }')  # what the bt-vol service writes
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
        check(r.cmd(cmd="audio_set", hw_gain=30)["ok"], "hardware input sensitivity accepted")
        a = r.wait(lambda a: a["choice"]["hw_gain"] == 30, "hw_gain kept")
        m = r.cmd(cmd="audio_set", hw_gain=150)
        check(not m["ok"] and m["code"] == "err.audio_gain", "sensitivity above 100 % refused")
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
        for typ, prm in (("wah", "pos"), ("octaver", "sub2")):
            m = r.cmd(cmd="add_block", type=typ)
            check(m["ok"], f"{typ} block can be added")
            check(r.cmd(cmd="set_param", id=m["id"], param=prm, value=50)["ok"], f"{typ}.{prm} can be set")
        wah = r.cmd(cmd="add_block", type="wah")["id"]
        check(r.cmd(cmd="expression", value=0.25)["applied"] is True, "expression drives the wah")
        with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
            blk = [b for b in json.load(resp)["chain"] if b["type"] == "wah"][0]  # the first wah in the chain is driven
        check(abs(blk["params"]["pos"] - 25.0) < 0.01, "wah position follows the expression value")
        check(r.cmd(cmd="preset_step", dir=1)["ok"], "preset_step loads a preset")
        check(r.cmd(cmd="preset_index", index=0)["ok"], "preset_index loads a preset")
        r.cmd(cmd="metronome", on=False)
        check(r.cmd(cmd="setlist_set", files=["b.json", "a.json"])["ok"], "setlist accepted")
        with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
            check(json.load(resp)["setlist"] == ["b.json", "a.json"], "setlist is kept in the given order")
        check(not r.cmd(cmd="setlist_set", files=["../x.json"])["ok"], "unsafe setlist entry refused")
        r.cmd(cmd="setlist_set", files=[])
        check(r.cmd(cmd="transport", speed=9, loop_a=1.0, loop_b=2.5)["ok"], "transport speed / A-B accepted")
        with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
            inp = json.load(resp)["input"]
        check(inp["speed"] == 1.5, "speed is clamped to 1.5")
        r.cmd(cmd="transport", speed=1.0, clear_loop=True)
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

    # MIDI: bytes appended to a fake device file act like a controller
    midi = tmp / "midi.bin"
    midi.write_bytes(b"")
    (tmp / "data" / "settings.json").write_text(json.dumps({"setlist": ["03_Dual_Amp.json", "01_Clean.json"]}))
    r = Run(tmp, env_extra={"NAMFX_MIDI_DEV": str(midi)})
    try:
        def preset_file():
            with urllib.request.urlopen(f"http://127.0.0.1:{r.port}/api/state", timeout=5) as resp:
                return json.load(resp)["preset"]["file"]
        with open(midi, "ab") as f:
            f.write(bytes([0xC0, 0x01]))  # program change 1 = second entry of the setlist
        end = time.time() + 8
        while time.time() < end and preset_file() != "01_Clean.json":
            time.sleep(0.1)
        check(preset_file() == "01_Clean.json", "MIDI program change loads the Nth preset of the setlist")
        with open(midi, "ab") as f:
            f.write(bytes([0xB0, 64, 127]))  # footswitch CC 64 = next preset (wraps to the first)
        end = time.time() + 8
        while time.time() < end and preset_file() != "03_Dual_Amp.json":
            time.sleep(0.1)
        check(preset_file() == "03_Dual_Amp.json", "MIDI CC 64 steps to the next preset")
    finally:
        r.stop()

    # mDNS: a plain DNS query for namfx.local gets this machine's address back
    mport = 15353
    r = Run(tmp, env_extra={"NAMFX_MDNS_PORT": str(mport)})
    try:
        q = bytes([0x12, 0x34, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]) + bytes([5]) + b"namfx" + bytes([5]) + b"local" + bytes([0, 0, 1, 0, 1])
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(2)
        got = None
        for _ in range(10):
            s.sendto(q, ("127.0.0.1", mport))
            try:
                got = s.recvfrom(1500)[0]
                break
            except socket.timeout:
                pass
        check(got is not None and got[0:2] == bytes([0x12, 0x34]) and got[2] & 0x80 and got[7] >= 1, "mDNS answers a query for namfx.local")
        check(got[-4:] != bytes([127, 0, 0, 1]), "the answer is a non-loopback address")
        s.close()
    finally:
        r.stop()

    # second audio thread: the pipelined / forked graph gives the same samples as the single threaded one
    def render(preset, cores):
        out = tmp / f"o_{preset}_{cores}.wav"
        p = subprocess.run([str(EXE), "--offline", str(ROOT / "data" / "wav" / "nam_example_di.wav"), str(out), "--preset", preset,
                            "--cores", str(cores), "--data", str(tmp / "data"), "--no-lv2"], capture_output=True, text=True, cwd=ROOT)
        raw = out.read_bytes()
        i = raw.find(b"data") + 8
        n = (len(raw) - i) // 4
        samples = struct.unpack("<%df" % n, raw[i:i + n * 4])
        plan = [l for l in p.stderr.splitlines() if l.startswith("[graph]")][-1].split()[1]
        return samples, plan

    for preset, want, shift in (("02_Plexi_Crunch.json", "pipeline", 128 * 2), ("03_Dual_Amp.json", "fork", 0)):
        one, plan1 = render(preset, 1)
        two, plan2 = render(preset, 2)
        check(plan1 == "serial", f"{preset}: one core is serial")
        check(plan2 == want, f"{preset}: two cores use {want} (got {plan2})")
        a = two[shift:]
        b = one[:len(one) - shift]
        diff = max(abs(x - y) for x, y in zip(a, b))
        check(diff < 1e-5, f"{preset}: threaded output equals the serial one" + (" (one block later)" if shift else "") + f", max diff {diff:.2e}")

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
