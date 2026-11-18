// Aether owns the UIKit window. This SDL backend renders into EAGL storage
// without installing a view, requesting orientation, or taking keyboard focus.
extern "C" {
#include "SDL_internal.h"
#include "video/SDL_sysvideo.h"
#include "events/SDL_windowevents_c.h"
}
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#include <dlfcn.h>
#include <new>

struct SDL_WindowData {
    EAGLContext* context = nil;
    GLuint framebuffer = 0, color = 0, depth = 0;
};

namespace {
bool resizeDrawable(SDL_Window* window, int width, int height) {
    auto& data = *window->internal;
    if (!data.context) return true; // SDL creates the context after the window.
    EAGLContext* previous = EAGLContext.currentContext;
    if (![EAGLContext setCurrentContext:data.context]) return SDL_SetError("Cannot activate EntisGLS EAGL drawable");
    GLint framebuffer = 0, renderbuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    if (!data.framebuffer) glGenFramebuffers(1, &data.framebuffer);
    if (!data.color) glGenRenderbuffers(1, &data.color);
    if (!data.depth) glGenRenderbuffers(1, &data.depth);
    glBindFramebuffer(GL_FRAMEBUFFER, data.framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, data.color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, data.color);
    glBindRenderbuffer(GL_RENDERBUFFER, data.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, data.depth);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    [EAGLContext setCurrentContext:previous];
    if (!complete) return SDL_SetError("EntisGLS EAGL framebuffer allocation failed (%d x %d)", width, height);
    // Reuse SDL's EAGL drawable properties so SDK render-target restoration and
    // frame readback follow the same contract as the standalone UIKit backend.
    const auto props = SDL_GetWindowProperties(window);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, data.framebuffer);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER, data.color);
    return true;
}

void destroyDrawable(SDL_WindowData& data) {
    if (!data.context) return;
    EAGLContext* previous = EAGLContext.currentContext;
    if ([EAGLContext setCurrentContext:data.context]) {
        glDeleteFramebuffers(1, &data.framebuffer);
        glDeleteRenderbuffers(1, &data.color);
        glDeleteRenderbuffers(1, &data.depth);
    }
    [EAGLContext setCurrentContext:previous == data.context ? nil : previous];
    data.framebuffer = data.color = data.depth = 0;
    data.context = nil;
}

SDL_GLContext createContext(SDL_VideoDevice* device, SDL_Window* window) {
    auto& data = *window->internal;
    if (data.context) {
        SDL_SetError("EntisGLS drawable already has a context; use a shared EAGL context for offscreen tasks");
        return nullptr;
    }
    EAGLContext* previous = EAGLContext.currentContext;
    if (device->gl_config.share_with_current_context && previous)
        data.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3 sharegroup:previous.sharegroup];
    else
        data.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!data.context) {
        SDL_SetError("EntisGLS requires OpenGL ES 3");
        return nullptr;
    }
    if (!resizeDrawable(window, window->w, window->h) || ![EAGLContext setCurrentContext:data.context]) {
        destroyDrawable(data);
        [EAGLContext setCurrentContext:previous];
        return nullptr;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, data.framebuffer);
    glViewport(0, 0, window->w, window->h);
    return (SDL_GLContext)(__bridge_retained void*)data.context;
}

bool makeCurrent(SDL_VideoDevice*, SDL_Window* window, SDL_GLContext handle) {
    EAGLContext* context = (__bridge EAGLContext*)(void*)handle;
    if (![EAGLContext setCurrentContext:context]) return SDL_SetError("Cannot activate EntisGLS EAGL context");
    if (context && window) glBindFramebuffer(GL_FRAMEBUFFER, window->internal->framebuffer);
    return true;
}

bool destroyContext(SDL_VideoDevice*, SDL_GLContext handle) {
    EAGLContext* context = (__bridge_transfer EAGLContext*)(void*)handle;
    if (EAGLContext.currentContext == context) [EAGLContext setCurrentContext:nil];
    return true;
}

SDL_VideoDevice* createDevice() {
    auto* device = (SDL_VideoDevice*)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) return nullptr;
    device->VideoInit = [](SDL_VideoDevice*) {
        SDL_DisplayMode mode{};
        mode.format = SDL_PIXELFORMAT_RGBA8888;
        mode.w = 1280; mode.h = 720;
        return SDL_AddBasicVideoDisplay(&mode) != 0;
    };
    device->VideoQuit = [](SDL_VideoDevice*) {};
    device->PumpEvents = [](SDL_VideoDevice*) {};
    device->free = [](SDL_VideoDevice* value) { SDL_free(value); };
    device->CreateSDLWindow = [](SDL_VideoDevice*, SDL_Window* window, SDL_PropertiesID) {
        window->internal = new (std::nothrow) SDL_WindowData;
        return window->internal != nullptr || SDL_OutOfMemory();
    };
    device->DestroyWindow = [](SDL_VideoDevice*, SDL_Window* window) {
        if (window->internal) destroyDrawable(*window->internal);
        delete window->internal;
        window->internal = nullptr;
    };
    device->SetWindowSize = [](SDL_VideoDevice*, SDL_Window* window) {
        if (resizeDrawable(window, window->pending.w, window->pending.h))
            SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
    };
    device->GL_LoadLibrary = [](SDL_VideoDevice*, const char*) { return true; };
    device->GL_UnloadLibrary = [](SDL_VideoDevice*) {};
    device->GL_GetProcAddress = [](SDL_VideoDevice*, const char* name) {
        return (SDL_FunctionPointer)dlsym(RTLD_DEFAULT, name);
    };
    device->GL_CreateContext = createContext;
    device->GL_MakeCurrent = makeCurrent;
    device->GL_DestroyContext = destroyContext;
    device->GL_SwapWindow = [](SDL_VideoDevice*, SDL_Window*) { glFlush(); return true; };
    device->GL_SetSwapInterval = [](SDL_VideoDevice*, int interval) {
        return interval == 0 || SDL_SetError("An offscreen drawable has no swap interval");
    };
    device->GL_GetSwapInterval = [](SDL_VideoDevice*, int* interval) { *interval = 0; return true; };
    return device;
}
}

extern "C" {
VideoBootStrap OFFSCREEN_bootstrap = {
    "offscreen", "Aether EntisGLS offscreen EAGL", createDevice, nullptr, false
};
}
