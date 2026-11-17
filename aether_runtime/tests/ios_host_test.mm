#define SDL_MAIN_HANDLED
#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "platform/sdl/gl_drawable.h"
#include "platform/ios/ios_gl_context.h"
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* operation) {
    if (!value) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
void verifyPixel(bool red) {
    unsigned char pixel[4]{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(glGetError() == GL_NO_ERROR && pixel[0] == (red ? 255 : 0) &&
            pixel[1] == (red ? 0 : 255) && pixel[3] == 255, "offscreen pixels");
}
void run(UIWindow* host) {
    SDL_SetMainReady();
    auto* controller = host.rootViewController;
    const auto views = host.subviews.count;
    for (int session = 0; session < 3; ++session) {
        require(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"), "select embedded video");
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS), "initialize without SDL UIApplication");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        auto* window = SDL_CreateWindow("offscreen", 96, 54, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window, "offscreen window");
        const auto context = SDL_GL_CreateContext(window);
        require(context, "EAGL context");
        require(host.keyWindow && host.rootViewController == controller && host.subviews.count == views,
                "host UIKit ownership preserved");
        int width = 0, height = 0;
        require(SDL_GetWindowSizeInPixels(window, &width, &height) && width == 96 && height == 54,
                "game drawable dimensions independent of screen");
        const auto framebuffer = study::platform::sdl::ResolveWindowFramebuffer(0);
        require(framebuffer != 0, "EAGL default framebuffer mapping");
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); verifyPixel(true);
        auto* shared = study::platform::sdl::CreateIOSSharedGLContext(context);
        require(shared && study::platform::sdl::MakeIOSGLContextCurrent(shared), "E-mote shared context");
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        const unsigned char color[] = {0, 255, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, color);
        study::platform::sdl::DestroyIOSSharedGLContext(shared);
        require(study::platform::sdl::MakeIOSGLContextCurrent(context), "restore drawable context");
        require(glIsTexture(texture), "E-mote textures shared with drawable");
        glDeleteTextures(1, &texture);
        verifyPixel(true);
        require(SDL_SetWindowSize(window, 64, 32), "resize offscreen window");
        require(SDL_GetWindowSizeInPixels(window, &width, &height) && width == 64 && height == 32,
                "resized drawable dimensions");
        glBindFramebuffer(GL_FRAMEBUFFER, study::platform::sdl::ResolveWindowFramebuffer(0));
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "resized attachments");
        glClearColor(0, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT); verifyPixel(false);
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
        auto* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        require(audio, "hosted audio stream");
        short silence[960]{};
        require(SDL_PutAudioStreamData(audio, silence, sizeof(silence)) && SDL_ResumeAudioStreamDevice(audio), "audio playback");
        require(SDL_PauseAudioStreamDevice(audio) && SDL_ResumeAudioStreamDevice(audio), "audio pause/resume");
        SDL_DestroyAudioStream(audio);
        require(SDL_GL_DestroyContext(context), "destroy context");
        SDL_DestroyWindow(window);
        SDL_Quit();
        require(host.keyWindow && host.rootViewController == controller && host.subviews.count == views,
                "host survives engine shutdown");
    }
}
}

@interface EntisHostTestDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end
@implementation EntisHostTestDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [UIViewController new];
    [self.window makeKeyAndVisible];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSMutableDictionary* report = [NSMutableDictionary dictionary];
        try { run(self.window); report[@"passed"] = @YES; }
        catch (const std::exception& error) { report[@"passed"] = @NO; report[@"error"] = @(error.what()); }
        NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        [[NSJSONSerialization dataWithJSONObject:report options:0 error:nil]
            writeToFile:[docs stringByAppendingPathComponent:@"result.json"] atomically:YES];
        NSLog(@"ENTIS_IOS_HOST_TEST: %@", report);
        exit([report[@"passed"] boolValue] ? 0 : 1);
    });
    return YES;
}
@end
int main(int argc, char** argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(EntisHostTestDelegate.class)); }
}
