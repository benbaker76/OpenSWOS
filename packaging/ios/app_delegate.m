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
#import <objc/runtime.h>

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

/*
 * The scene life cycle (GitHub issue 27, 2026-11-20). An app built with the iOS 27 SDK that does not
 * adopt it is stopped at launch ("UIScene life cycle is required for apps built with this SDK":
 * a black screen, then gone). SDL 2.30 knows nothing of scenes, so the app does the two things
 * that takes:
 *   - Info.plist's UIApplicationSceneManifest names OpenSWOSSceneDelegate, which keeps the scene and
 *     hands every link to the app delegate above -- under scenes iOS gives links to the scene
 *     (openURLContexts, and the launch's in its connection options), never to application:openURL:.
 *   - SDL's window (SDL_uikitwindow, made with initWithFrame: in UIKit_CreateWindow) is made IN
 *     that scene: a window with no scene is never shown under the scene life cycle.
 * SDL hears of the background and foreground through UIApplication's notifications, which iOS still
 * posts for a one-scene app, so nothing else changes. (SDL's own launch-screen window has no scene
 * and is not shown: the UILaunchScreen covers the same moment.)
 */
static UIWindowScene *s_scene;

static UIWindowScene *current_scene(void)
{
    if (!s_scene)
        for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
            if ([s isKindOfClass:UIWindowScene.class]) {
                s_scene = (UIWindowScene *)s;
                break;
            }
    return s_scene;
}

/* Every other window SDL makes with initWithFrame: -- the message boxes' (SDL_uikitmessagebox.m:
 * "No game data" and the setup's questions, shown before the game has a window), the launch
 * screen's -- joins the scene too, at its size: without one a message box is never shown and
 * SDL_ShowMessageBox waits for an answer nobody can give, on a black screen. */
static id (*s_windowInitWithFrame)(id, SEL, CGRect);

static id window_initWithFrame(UIWindow *self, SEL cmd, CGRect frame)
{
    self = s_windowInitWithFrame(self, cmd, frame);
    UIWindowScene *scene = current_scene();
    if (self && scene && !self.windowScene) {
        self.windowScene = scene;
        self.frame = scene.coordinateSpace.bounds;
    }
    return self;
}

@interface OpenSWOSSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation OpenSWOSSceneDelegate

+ (void)load
{
    Method m = class_getInstanceMethod(UIWindow.class, @selector(initWithFrame:));
    s_windowInitWithFrame = (id (*)(id, SEL, CGRect))method_setImplementation(m, (IMP)window_initWithFrame);
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions
{
    if ([scene isKindOfClass:UIWindowScene.class])
        s_scene = (UIWindowScene *)scene;
    /* The link that started the app: kept for the start-up's data check, as launchOptions' was
     * (SDL is not running yet, so there is no drop event to send it as). */
    for (UIOpenURLContext *c in connectionOptions.URLContexts)
        Mobile_LinkKept(c.URL.absoluteString.UTF8String);
}

- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)contexts
{
    id<UIApplicationDelegate> d = UIApplication.sharedApplication.delegate;
    for (UIOpenURLContext *c in contexts)       /* Mobile_LinkKept, then SDL's drop event */
        [d application:UIApplication.sharedApplication openURL:c.URL options:@{}];
}

- (UIWindow *)window                            /* SDL's, which its app delegate knows */
{
    return UIApplication.sharedApplication.delegate.window;
}

- (void)setWindow:(UIWindow *)window
{
}

@end

@interface SDL_uikitwindow : UIWindow
@end

@implementation SDL_uikitwindow (OpenSWOSScene)

- (instancetype)initWithFrame:(CGRect)frame
{
    /* SDL_main starts on the run loop's first pass after launching, by when the scene has
     * connected; should it ever not have, give it two seconds before making a window without one. */
    UIWindowScene *scene = current_scene();
    for (int i = 0; !scene && i < 200; i++) {
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        scene = current_scene();
    }
    if (!scene)
        return [super initWithFrame:frame];
    /* The scene's own size, not SDL's frame: that is UIScreen's bounds, still portrait while a
     * landscape-only scene is coming up, and a window made that size drew a 393x852 game. */
    return [super initWithWindowScene:scene];
}

@end
