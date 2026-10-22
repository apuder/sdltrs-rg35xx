#include "rg35xx_bridge.h"

#include "KeyboardRG35XXControls.hpp"
#include "RG35XXInputRouter.hpp"
#include "SoftKeyboardRenderer.hpp"
#include "TrsKeyboardAdapter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

extern "C" {
#include "../trs_sdl_keyboard.h"
}

namespace {

class EmulatorGuestKeySink : public rg35xx::GuestKeySink {
public:
    int keyIdentity(int symbol) const override
    {
        return trs_key_identity(symbol);
    }

    void keyDown(int symbol) override
    {
        trs_xlate_keysym(symbol);
    }

    void keyUp(int symbol) override
    {
        trs_xlate_keysym(0x10000 | symbol);
    }
};

std::unique_ptr<rg35xx::KeyboardRG35XXControls> controls;
std::unique_ptr<EmulatorGuestKeySink> sink;
std::unique_ptr<rg35xx::TrsKeyboardAdapter> keyboard;
std::unique_ptr<rg35xx::RG35XXInputRouter> router;
rg35xx::SoftKeyboardRenderer overlay_renderer;
unsigned int modal_depth;
bool shutdown_registered;

void reportException(const char* operation)
{
    std::fprintf(stderr, "SDLTRS RG35XX: %s failed; simulator disabled\n",
                 operation);
}

} // namespace

extern "C" int rg35xx_bridge_init(void)
{
    const char* mode = std::getenv("SDLTRS_RG35XX_INPUT");
    if (mode == NULL || mode[0] == '\0' || std::strcmp(mode, "off") == 0)
        return 0;
    if (std::strcmp(mode, "keyboard") != 0) {
        std::fprintf(stderr,
            "SDLTRS RG35XX: unsupported SDLTRS_RG35XX_INPUT='%s' "
            "(expected 'off' or 'keyboard'); input disabled\n", mode);
        return 0;
    }

    try {
        controls.reset(new rg35xx::KeyboardRG35XXControls());
        sink.reset(new EmulatorGuestKeySink());
        keyboard.reset(new rg35xx::TrsKeyboardAdapter(*sink));
        router.reset(new rg35xx::RG35XXInputRouter(*controls, *keyboard));
        modal_depth = 0;
        if (!shutdown_registered) {
            if (std::atexit(rg35xx_bridge_shutdown) != 0) {
                reportException("shutdown registration");
                router.reset();
                keyboard.reset();
                sink.reset();
                controls.reset();
                return 0;
            }
            shutdown_registered = true;
        }
        std::fprintf(stderr,
            "SDLTRS RG35XX: host-keyboard simulator enabled (F6 toggles overlay)\n");
        return 1;
    } catch (...) {
        reportException("initialization");
        router.reset();
        keyboard.reset();
        sink.reset();
        controls.reset();
        return 0;
    }
}

extern "C" int rg35xx_bridge_enabled(void)
{
    return router.get() != NULL;
}

extern "C" void rg35xx_bridge_shutdown(void)
{
    try {
        if (router)
            router->lifecycleCleanup(SDL_GetTicks());
        router.reset();
        keyboard.reset();
        sink.reset();
        controls.reset();
        modal_depth = 0;
    } catch (...) {
        reportException("shutdown");
    }
}

extern "C" int rg35xx_bridge_handle_event(const SDL_Event* event,
                                             uint32_t now_ms)
{
    if (!router || event == NULL || modal_depth != 0)
        return 0;
    try {
        return router->handleSDLEvent(*event, now_ms) ? 1 : 0;
    } catch (...) {
        reportException("event handling");
        rg35xx_bridge_shutdown();
        return 0;
    }
}

extern "C" void rg35xx_bridge_update(uint32_t now_ms)
{
    if (!router || modal_depth != 0)
        return;
    try {
        router->update(now_ms);
    } catch (...) {
        reportException("timer update");
        rg35xx_bridge_shutdown();
    }
}

extern "C" int rg35xx_bridge_dirty(void)
{
    return router && router->dirty();
}

extern "C" void rg35xx_bridge_draw(SDL_Renderer* renderer)
{
    if (!router)
        return;
    try {
        if (router->visible())
            overlay_renderer.draw(renderer, router->softKeyboard());
        router->clearDirty();
    } catch (...) {
        reportException("overlay drawing");
        rg35xx_bridge_shutdown();
    }
}

extern "C" void rg35xx_bridge_modal_begin(uint32_t now_ms)
{
    if (!router)
        return;
    try {
        if (modal_depth++ == 0)
            router->setSuspended(true, now_ms);
    } catch (...) {
        reportException("modal entry");
        rg35xx_bridge_shutdown();
    }
}

extern "C" void rg35xx_bridge_modal_end(uint32_t now_ms)
{
    if (!router || modal_depth == 0)
        return;
    try {
        if (--modal_depth == 0)
            router->setSuspended(false, now_ms);
    } catch (...) {
        reportException("modal exit");
        rg35xx_bridge_shutdown();
    }
}

extern "C" void rg35xx_bridge_lifecycle_cleanup(uint32_t now_ms)
{
    if (!router)
        return;
    try {
        router->lifecycleCleanup(now_ms);
    } catch (...) {
        reportException("lifecycle cleanup");
        rg35xx_bridge_shutdown();
    }
}

extern "C" int rg35xx_bridge_guest_key_event(int owner, int symbol, int down)
{
    if (!router || !rg35xx::TrsKeyboardAdapter::validSymbol(symbol))
        return 0;
    try {
        router->guestKeyEvent(owner, symbol, down != 0);
        return 1;
    } catch (...) {
        reportException("guest key routing");
        rg35xx_bridge_shutdown();
        return 0;
    }
}

extern "C" int rg35xx_bridge_physical_owner(int scancode)
{
    return rg35xx::GuestOwnerPhysicalBase + scancode;
}
