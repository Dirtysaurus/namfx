// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
//
// LV2 plugin host (layer 3). Plugins are installed by the user (system packages or
// data/lv2); namfx only loads them. Built only when lilv is available
// (NAMFX_HAVE_LILV), otherwise the scan finds nothing.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "dsp/blocks.h"

namespace fx {

// Scans the LV2 path (+ userDir) and appends one BlockDef per usable plugin.
// Skipped plugins are reported in log as {"lv2": uri, "skipped": reason}.
void lv2Scan(const std::string& userDir, std::vector<std::unique_ptr<BlockDef>>& out, json& log);
bool lv2Available();
std::string lv2Path(const std::string& userDir);

}  // namespace fx
