/*
 * app_delegate.m -- the link the app was STARTED with (OpenSWOS Setup's Wi-Fi copy), kept.
 *
 * iOS hands a URL to the app delegate's openURL; SDL turns it into an SDL_DROPFILE. But when the
 * link is what starts the app, iOS calls openURL during the launch, before SDL_main has run
 * SDL_Init: there is no event queue yet and SDL_SendDropFile throws it away. The app then started
 * straight into the game, the copy never asked for (2026-11-03, iPod touch, iOS 15).
 *
 * So the app delegate is SDL's own, subclassed (the way SDL_uikitappdelegate.m says to: a
 * category returning the subclass's name), and it gives every URL to Mobile_LinkKept
 * (src/mobile.c) first -- the launch options' too -- before SDL's handling. That keeps a setup
 * link for the start-up's data check and ignores anything else; a link that comes while the
 * game plays still reaches Mobile_LinkDropped through SDL's drop event.
 */
#import <UIKit/UIKit.h>

@interface SDLUIKitDelegate : NSObject <UIApplicationDelegate>
+ (NSString *)getAppDelegateClassName;
@end

extern void Mobile_LinkKept(const char *link);

@interface OpenSWOSAppDelegate : SDLUIKitDelegate
@end

@implementation OpenSWOSAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    NSURL *url = launchOptions[UIApplicationLaunchOptionsURLKey];
    if (url)
        Mobile_LinkKept(url.absoluteString.UTF8String);
    return [super application:application didFinishLaunchingWithOptions:launchOptions];
}

- (BOOL)application:(UIApplication *)app openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options
{
    Mobile_LinkKept(url.absoluteString.UTF8String);
    return [super application:app openURL:url options:options];
}

@end

@implementation SDLUIKitDelegate (OpenSWOS)
+ (NSString *)getAppDelegateClassName
{
    return @"OpenSWOSAppDelegate";
}
@end
