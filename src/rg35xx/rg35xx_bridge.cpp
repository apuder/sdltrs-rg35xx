#include "rg35xx_bridge.h"

#include "KeyboardRG35XXControls.hpp"
#include "RG35XXInputRouter.hpp"
#include "SDLRG35XXControls.hpp"
#include "SoftKeyboardRenderer.hpp"
#include "TrsKeyboardAdapter.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
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

std::unique_ptr<rg35xx::RG35XXControls> controls;
std::unique_ptr<EmulatorGuestKeySink> sink;
std::unique_ptr<rg35xx::TrsKeyboardAdapter> keyboard;
std::unique_ptr<rg35xx::RG35XXInputRouter> router;
rg35xx::SoftKeyboardRenderer overlay_renderer;
unsigned int modal_depth;
bool shutdown_registered;

void reportException(const char* operation)
{
    std::fprintf(stderr, "SDLTRS RG35XX: %s failed; input disabled\n",
                 operation);
}

bool parseDeviceIndex(const char* text, int& index)
{
    if (text == NULL || text[0] == '\0') {
        index = 0;
        return true;
    }
    errno = 0;
    char* end = NULL;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 ||
        value > std::numeric_limits<int>::max())
        return false;
    index = static_cast<int>(value);
    return true;
}

bool validGUID(const char* text)
{
    if (text == NULL || text[0] == '\0')
        return true;
    if (std::strlen(text) != 32)
        return false;
    for (std::size_t i = 0; text[i] != '\0'; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(text[i])))
            return false;
    }
    return true;
}

} // namespace

extern "C" int rg35xx_bridge_init(void)
{
    if (router)
        rg35xx_bridge_shutdown();
    const char* mode = std::getenv("SDLTRS_RG35XX_INPUT");
    if (mode == NULL || mode[0] == '\0' || std::strcmp(mode, "off") == 0)
        return 0;
    const bool keyboard_mode = std::strcmp(mode, "keyboard") == 0;
    const bool sdl_mode = std::strcmp(mode, "sdl") == 0;
    if (!keyboard_mode && !sdl_mode) {
        std::fprintf(stderr,
            "SDLTRS RG35XX: unsupported SDLTRS_RG35XX_INPUT='%s' "
            "(expected 'off', 'keyboard', or 'sdl'); input disabled\n", mode);
        return 0;
    }

    try {
        bool reserve_all_when_hidden = true;
        if (keyboard_mode) {
            controls.reset(new rg35xx::KeyboardRG35XXControls());
        } else {
            const char* map_text = std::getenv("SDLTRS_RG35XX_MAP");
            rg35xx::SDLJoystickMapping mapping;
            std::string mapping_error;
            if (map_text == NULL ||
                !rg35xx::SDLJoystickMapping::parse(
                    map_text != NULL ? map_text : "", mapping,
                    mapping_error)) {
                std::fprintf(stderr,
                    "SDLTRS RG35XX: SDLTRS_RG35XX_INPUT=sdl requires a valid "
                    "SDLTRS_RG35XX_MAP: %s; input disabled\n",
                    mapping_error.c_str());
                return 0;
            }

            int device_index = 0;
            const char* device_text = std::getenv("SDLTRS_RG35XX_DEVICE");
            if (!parseDeviceIndex(device_text, device_index)) {
                std::fprintf(stderr,
                    "SDLTRS RG35XX: invalid SDLTRS_RG35XX_DEVICE='%s' "
                    "(expected a non-negative SDL joystick index); "
                    "input disabled\n", device_text);
                return 0;
            }
            const char* guid_text = std::getenv("SDLTRS_RG35XX_GUID");
            if (!validGUID(guid_text)) {
                std::fprintf(stderr,
                    "SDLTRS RG35XX: invalid SDLTRS_RG35XX_GUID='%s' "
                    "(expected 32 hexadecimal digits); input disabled\n",
                    guid_text);
                return 0;
            }

            std::unique_ptr<rg35xx::SDLRG35XXControls> physical(
                new rg35xx::SDLRG35XXControls(
                    mapping, device_index,
                    guid_text != NULL ? guid_text : ""));
            if (physical->connected()) {
                std::fprintf(stderr,
                    "SDLTRS RG35XX: physical SDL backend enabled for %s\n",
                    physical->deviceName().c_str());
            } else {
                std::fprintf(stderr,
                    "SDLTRS RG35XX: physical SDL backend enabled but waiting: "
                    "%s\n", physical->lastError().c_str());
            }
            controls.reset(physical.release());
            reserve_all_when_hidden = false;
        }
        sink.reset(new EmulatorGuestKeySink());
        keyboard.reset(new rg35xx::TrsKeyboardAdapter(*sink));
        router.reset(new rg35xx::RG35XXInputRouter(
            *controls, *keyboard, reserve_all_when_hidden));
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
        if (keyboard_mode) {
            std::fprintf(stderr,
                "SDLTRS RG35XX: host-keyboard simulator enabled "
                "(F6 toggles overlay)\n");
        }
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
