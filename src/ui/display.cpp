// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Peter Hudak
#include "ui/platform.h"

#ifdef NAMFX_HAVE_SDL
#include <SDL.h>
#endif

namespace ui {

#ifdef NAMFX_HAVE_SDL
bool Display::open(int w, int h, std::string& err) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { err = SDL_GetError(); return false; }
  SDL_ShowCursor(SDL_DISABLE);
  auto* win = SDL_CreateWindow("namfx", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
  if (!win) { err = SDL_GetError(); SDL_Quit(); return false; }
  auto* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { err = SDL_GetError(); SDL_DestroyWindow(win); SDL_Quit(); return false; }
  SDL_RenderSetLogicalSize(ren, w, h);
  auto* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
  win_ = win;
  ren_ = ren;
  tex_ = tex;
  return true;
}

void Display::present(const Canvas& c) {
  if (!ren_) return;
  SDL_UpdateTexture((SDL_Texture*)tex_, nullptr, c.pixels(), c.width() * 4);
  SDL_RenderClear((SDL_Renderer*)ren_);
  SDL_RenderCopy((SDL_Renderer*)ren_, (SDL_Texture*)tex_, nullptr, nullptr);
  SDL_RenderPresent((SDL_Renderer*)ren_);
}

bool Display::pollKeys(const BtnFn& fn) {
  SDL_Event e;
  bool alive = true;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) alive = false;
    if (e.type != SDL_KEYDOWN) continue;
    const bool rep = e.key.repeat != 0;
    switch (e.key.keysym.sym) {
      case SDLK_UP: fn(Btn::Up, rep); break;
      case SDLK_DOWN: fn(Btn::Down, rep); break;
      case SDLK_LEFT: fn(Btn::Left, rep); break;
      case SDLK_RIGHT: fn(Btn::Right, rep); break;
      case SDLK_RETURN: case SDLK_a: fn(Btn::A, rep); break;
      case SDLK_ESCAPE: case SDLK_b: fn(Btn::B, rep); break;
      case SDLK_x: fn(Btn::X, rep); break;
      case SDLK_y: fn(Btn::Y, rep); break;
      case SDLK_q: fn(Btn::L1, rep); break;
      case SDLK_w: fn(Btn::R1, rep); break;
      case SDLK_1: fn(Btn::L2, rep); break;
      case SDLK_2: fn(Btn::R2, rep); break;
      case SDLK_TAB: fn(Btn::Select, rep); break;
      case SDLK_SPACE: fn(Btn::Start, rep); break;
      default: break;
    }
  }
  return alive;
}

void Display::close() {
  if (tex_) SDL_DestroyTexture((SDL_Texture*)tex_);
  if (ren_) SDL_DestroyRenderer((SDL_Renderer*)ren_);
  if (win_) { SDL_DestroyWindow((SDL_Window*)win_); SDL_Quit(); }
  tex_ = ren_ = win_ = nullptr;
}
#else
bool Display::open(int, int, std::string& err) { err = "built without SDL2"; return false; }
void Display::present(const Canvas&) {}
bool Display::pollKeys(const BtnFn&) { return true; }
void Display::close() {}
#endif

}  // namespace ui
