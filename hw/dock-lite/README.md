# namfx dock – Lite (rev 0.1, schematic only)

Guitar buffer plus one dual-role USB-C port for the R36S. No hub, no WiFi.

- **Guitar in:** 6.35 mm jack → Hi-Z buffer (MCP6022, gain 2–12×) → console MIC (TRRS ring 2, OMTP).
- **Headphones:** pass-through of console T/R1 to a 3.5 mm socket.
- **EXT USB-C (DRP, TUSB320LAI in GPIO mode):**
  - charger attached → switch A charges the console through its charging port
  - USB device attached → switch B powers it from the console OTG port; data go straight to OTG
  - dead console → TUSB320 presents Rd unpowered, so the charger still turns on
- **Console side:** two USB-C plug pigtails (OTG, charging) and one TRRS pigtail. Jacks are panel-mount, wired to headers.

| File | |
|---|---|
| `namfx_dock_lite.kicad_sch` | schematic (KiCad 8+), symbols embedded |
| `bom.csv` | BOM grouped by value/footprint |
| `netlist.txt` | net → pins, for review |
| `docs/preview.png` | rough preview (not KiCad's renderer) |
| `tools/gen_sch.py` | generator – the component table in it is the source of truth |

Regenerate: `python3 tools/gen_sch.py && python3 tools/preview.py docs/preview.png`

## Check before layout

1. **Console VBUS:** with the battery disconnected, the OTG VBUS and the charging-port VBUS must **not** be connected inside the console.
2. **OTG host:** confirm the console supplies 5 V on OTG VBUS when CC sees 5.1 kΩ Rd (R9).
3. **Footprints:**
   - `USB_C_Receptacle_HRO_TYPE-C-31-M-12`
   - `Texas_X2QFN-12_1.6x1.6mm_P0.4mm` (TUSB320LAI)
   - the 3314J trimmer
4. **Q1 (AO4805) power:** dissipation is I² × ~2 × 35 mΩ. That is fine up to about 1.5 A of charging current.
5. **Output level:** the gain range 2–12× assumes the RK817 MIC preamp is set low. Measure it with real DI levels.
