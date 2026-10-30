// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#pragma once
#include <string>
#include <vector>
#include "engine.h"
#include "ui/canvas.h"

namespace ui {

enum class Btn { Up, Down, Left, Right, A, B, X, Y, L1, R1, L2, R2, Select, Start, None };

// 640x480 on-screen UI driven by buttons. Draws into a Canvas, reads data from the Engine.
class Screens {
 public:
  Screens(fx::Engine& e, const std::string& fontDir);
  void press(Btn b, bool repeat = false);
  void setButtonsHeld(bool select, bool start) { selHeld_ = select; startHeld_ = start; }
  bool quit() const { return quit_; }
  // returns true when a redraw is needed
  bool update();
  void render(Canvas& c);
  void setNetInfo(const std::string& s) { net_ = s; }
  // for snapshots
  enum class Mode { Rig, Edit, Add, Files, Tuner, Menu, Presets };
  void forceMode(Mode m) { mode_ = m; dirty_ = true; }
  void selectId(const std::string& id);

 private:
  struct Cell { int col, row; std::string id; char kind; std::string split; };  // kind: b block, s split, m merge, + slot
  void refresh();
  void layout();
  const fx::json* blockJson(const std::string& id) const;
  const fx::json* defOf(const std::string& type) const;
  std::vector<int> visibleParams(const fx::json& def) const;  // indices of non-hidden parameters
  std::vector<const fx::json*> addList() const;               // Add screen: filtered effects of the category
  void drawPedal(Canvas& c, float cx, float cy, const fx::json& b, const fx::json& d, bool sel, const std::string& label);
  std::string fmtParam(const fx::json& pdef, float v) const;
  void cmd(const fx::json& c);
  void toast(const std::string& s);
  void adjustParam(int dir, bool fast);

  void drawTop(Canvas& c);
  void drawFooter(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints);
  void drawRig(Canvas& c);
  void drawEdit(Canvas& c);
  void drawAdd(Canvas& c);
  void drawFiles(Canvas& c);
  void drawTuner(Canvas& c);
  struct MenuEntry { std::string label; int id; };  // id: action, 99 = back, 100+n = open page n
  std::vector<MenuEntry> menuEntries();
  void drawMenu(Canvas& c);
  std::string audioLabel() const;
  void cycleAudio();
  void drawPresets(Canvas& c);
  void drawKnob(Canvas& c, float cx, float cy, float r, float frac, Color col, bool active);

  fx::Engine& eng_;
  Font sans_, bold_, mono_;
  fx::json state_, status_, catalog_;
  std::string lang_;
  unsigned seenVersion_ = 0;
  std::vector<Cell> cells_;
  int cur_ = 0;
  std::string curId_;
  int scroll_ = 0;
  Mode mode_ = Mode::Rig;
  int editSel_ = 0, addCat_ = 0, addFx_ = 0, fileSel_ = 0, menuSel_ = 0, menuPage_ = 0, presetSel_ = 0;
  int addSrc_ = 0;           // 0 all, 1 built-in + bundled types, 2 user types, 3 LV2
  bool addFocusList_ = false;
  unsigned catalogVersion_ = 0;
  bool pedals_ = false;      // rig drawn as a pedalboard
  std::string fileKind_, fileBlock_;
  std::string addAfter_;
  fx::json addLane_;
  bool quit_ = false, dirty_ = true, selHeld_ = false, startHeld_ = false;
  std::string net_, toast_;
  int toastTicks_ = 0;
  unsigned frame_ = 0;
};

}  // namespace ui
