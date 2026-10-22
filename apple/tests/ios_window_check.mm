#include "../ios_window.h"
#include "../touch_controls.h"
#include <cstring>
#include <cstdlib>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

void ios_check_window_geometry(SDL_Window *window, SDL_Renderer *renderer)
{
#ifndef NDEBUG
    const char *requested = SDL_getenv("VF3_TEST_WINDOW_GEOMETRY");
    if (!requested || strcmp(requested, "1") != 0) return;
    UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    UIView *view = native.rootViewController.view;
    const CGRect scene = native.windowScene.coordinateSpace.bounds;
    auto require = [](bool ok, const char *message) {
        if (!ok) { fprintf(stderr, "iOS geometry check FAILED: %s\n", message); abort(); }
    };
    require(scene.size.width > scene.size.height, "test scene must be landscape");
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0, "desktop fullscreen disabled");
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS) != 0, "scene uses borderless presentation");
    // Reproduce the stale portrait window and Metal root view from a launch
    // during rotation. Both renderers must recover, as must the native overlay.
    const CGRect portrait = CGRectMake(0, 0, scene.size.height, scene.size.width);
    native.frame = portrait;
    view.frame = portrait;
    [view setNeedsLayout];
    [view layoutIfNeeded];
    require(view.bounds.size.width < view.bounds.size.height, "portrait fault injected");
    ios_sync_game_window(window);
    require(CGSizeEqualToSize(native.bounds.size, scene.size), "window fills scene");
    require(CGSizeEqualToSize(view.bounds.size, scene.size), "root view fills window");
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    require(w == int(scene.size.width) && h == int(scene.size.height), "SDL point size updated");
    SDL_GetRenderOutputSize(renderer, &w, &h);
    require(w > h, "renderer output is landscape");
    if ([view.layer isKindOfClass:CAMetalLayer.class]) {
        const CGSize drawable = ((CAMetalLayer *)view.layer).drawableSize;
        require(int(drawable.width) == w && int(drawable.height) == h, "Metal drawable matches renderer");
    }
    bool found = false;
    for (UIView *child in view.subviews) {
        if (![child.accessibilityIdentifier isEqualToString:@"vf3.touch-controls"]) continue;
        found = true;
        require(CGRectEqualToRect(child.frame, view.bounds), "touch overlay fills root view");
        const UIEdgeInsets inset = child.safeAreaInsets;
        const CGRect safe = UIEdgeInsetsInsetRect(child.bounds, inset);
        touch_controls::box buttons[touch_controls::ACTIONS];
        touch_controls::layout({float(safe.origin.x), float(safe.origin.y),
            float(safe.size.width), float(safe.size.height)}, buttons, true);
        const auto &back = buttons[touch_controls::MENU].r;
        const auto &resume = buttons[touch_controls::START].r;
        require(back.x + back.w < resume.x, "Back and Resume do not overlap");
    }
    require(found, "native controls attached");
    fprintf(stderr, "iOS geometry check PASSED: portrait fault recovered, SDL/Metal/controls %dx%d\n", w, h);
#endif
}
