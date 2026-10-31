# R36S system scripts (WiFi, Bluetooth, audio routing)

The console's own helper scripts, as they run on the owner's R36S under dArkOS. namfx does not need
them to build, but its Bluetooth output (*System → Audio input → Output → Bluetooth headphones*) relies on
what they set up. They are kept here as a reference and a backup; they are **not** installed by namfx.

Hardware: the internal **RTL8723BU** carries WiFi (`8723bu`, loaded with `rtw_btcoex_enable=1`) and Bluetooth
(`rtk_btusb mp_drv_mode=1`) on one USB device (`1-1`), which is also the single OTG port.

| Path | What it does |
|---|---|
| `scripts/Absolute.sh` | **R36S Manager v71**, the current menu in `/opt/system`: WiFi + BT, WiFi only, BT only, all off; WiFi manager (scan, saved, delete, info); Bluetooth manager (pair, connect, forget, test sound); SSH/Samba/filebrowser; quick terminal. Starts / stops the audio services. |
| `scripts/WiFi BT Toggle.sh`, `scripts/FIXALL.sh` | Older menu (v38, for an RTL8821CU dongle) and the repair script that recreates it. Kept for reference. |
| `scripts/bt_volume_monitor.sh` | Run by `bt-vol.service`. When a headphone is connected it loads `snd-aloop`, writes `/etc/asound.conf` (`default` → `hw:Loopback,0,0`, `blueout` → bluealsa with the headphone's MAC) and starts `alsaloop -C hw:Loopback,1,0 -P blueout`. When the headphones disappear it kills `alsaloop` and removes `/etc/asound.conf`. |
| `scripts/bt_vol_sync.sh` | Copies the console's volume to the Bluetooth volume. |
| `scripts/clean_boot.sh`, `services/r36s-radio-off.service` | Boot with both radios off (the menu switches them on). |
| `scripts/switch_to_bt.sh`, `switch_debug.sh`, `auto_test.sh` | Background Bluetooth switch (USB reset, module reload) and its debug helpers. |
| `scripts/Backup.sh`, `scripts/restore.sh` | Back up / restore the whole setup (modules, firmware, services, configs) to `/roms/backup`. |
| `services/bluealsa.service` | `bluealsad -p a2dp-source -p a2dp-sink`. |
| `services/bt-vol.service` | Starts `bt_volume_monitor.sh`. |
| `config/8723bu.conf`, `config/blacklist-rtl8xxxu.conf` | Load the vendor `8723bu` driver, keep the in-kernel `rtl8xxxu` away. |
| `config/asoundrc.dmix.example` | Plain dmix setup for the internal codec. |
| `tools/fake_headphone.py` | Injects the "headphone inserted" switch event so the speaker mutes. |
| `tools/dump.sh` | Writes a text dump of all of the above plus module / sound card status to `/roms`. |

How namfx uses it: with *Output → Bluetooth headphones*, namfx plays to ALSA `default` while
`/etc/asound.conf` mentions `Loopback` (i.e. headphones are connected) and to the sound card otherwise.

## Not included

The kernel modules (`8723bu.ko`, `rtk_btusb.ko`, `snd-aloop.ko`), the `bluealsad` binary, the Realtek
firmware blobs, ALSA plugin libraries, RetroArch / EmulationStation configs and the dArkOS performance
scripts: they are binaries or someone else's files. `Backup.sh` copies them to `/roms/backup` on the console.

## License

The scripts are by the namfx author and fall under the project's GPL-3.0-only licence, like the rest of the
repository (they are shell scripts without the usual SPDX header, unchanged from the console).
