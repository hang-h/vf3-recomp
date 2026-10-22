#pragma once
#include <SDL3/SDL.h>

// Main thread only. Use UIWindowScene geometry, including during the handoff
// from the ROM importer, rather than the screen's pre-rotation dimensions.
void ios_prepare_game_window(SDL_Window *window);
void ios_show_game_window(SDL_Window *window);
void ios_sync_game_window(SDL_Window *window);
void ios_check_window_geometry(SDL_Window *window, SDL_Renderer *renderer);
