#include "ios_window.h"
#include "ios_roms.h"
#import <UIKit/UIKit.h>

void ios_sync_game_window(SDL_Window *window)
{
    @autoreleasepool {
        UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        UIWindowScene *scene = native.windowScene;
        UIViewController *controller = native.rootViewController;
        if (!scene || !controller || controller.transitionCoordinator) return;

        // A hidden SDL window/Metal view can retain portrait bounds while the
        // importer has already rotated its scene to landscape. The scene's
        // coordinate space also respects the actual window size on iPad.
        const CGRect frame = scene.coordinateSpace.bounds;
        if (CGRectIsEmpty(frame)) return;
        bool changed = false;
        if (!CGRectEqualToRect(native.frame, frame)) {
            native.frame = frame;
            changed = true;
        }
        UIView *view = controller.view;
        if (!CGRectEqualToRect(view.frame, native.bounds)) {
            view.frame = native.bounds;
            changed = true;
        }
        view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        if (changed) {
            [native setNeedsLayout];
            [view setNeedsLayout];
            [native layoutIfNeeded];
            [view layoutIfNeeded];
            // SDL's controller publishes WINDOW_RESIZED during this layout;
            // its Metal view updates drawableSize from layoutSubviews.
            fprintf(stderr, "iOS window geometry corrected: scene %.0fx%.0f, view %.0fx%.0f points\n",
                frame.size.width, frame.size.height, view.bounds.size.width, view.bounds.size.height);
        }
    }
}

void ios_prepare_game_window(SDL_Window *window)
{
    // Size the hidden window from its live scene before a renderer replaces
    // the root view. Swapping SDL's root while it is appearing causes unbalanced
    // UIKit appearance callbacks, so make it visible only after that swap.
    ios_sync_game_window(window);
}

void ios_show_game_window(SDL_Window *window)
{
    SDL_ShowWindow(window);
    ios_finish_loading();
    SDL_PumpEvents();
    ios_sync_game_window(window);
}
