#ifdef __APPLE__
#include <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

void set_macos_window_style(SDL_Window *window) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (SDL_GetWindowWMInfo(window, &info)) {
    if (info.subsystem == SDL_SYSWM_COCOA) {
      NSWindow *nsWindow = info.info.cocoa.window;
      // Apply FullSizeContentView to hide titlebar but keep buttons
      [nsWindow setStyleMask:[nsWindow styleMask] |
                             (1 << 15)]; // NSWindowStyleMaskFullSizeContentView
      [nsWindow setTitlebarAppearsTransparent:YES];
      [nsWindow setTitleVisibility:1]; // NSWindowTitleHidden
    }
  }
}
#else
#include <SDL2/SDL.h>
void set_macos_window_style(SDL_Window *window) { (void)window; }
#endif
