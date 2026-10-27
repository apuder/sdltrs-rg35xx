#include "../src/rg35xx/KeyboardRG35XXControls.hpp"
#include "../src/rg35xx/RG35XXInputRouter.hpp"
#include "../src/rg35xx/SDLRG35XXControls.hpp"
#include "../src/rg35xx/rg35xx_bridge.h"
#include "../src/rg35xx/SystemVolume.hpp"
#ifndef RG35XX_TEST_NO_RENDERER
#include "../src/rg35xx/SoftKeyboardRenderer.hpp"
#endif
#include "../src/rg35xx/TrsKeyboardAdapter.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>
#include <utility>
#include <vector>

using namespace rg35xx;

static std::vector<int> bridge_guest_events;
static std::uint64_t bridge_mapped_keys;

extern "C" void trs_kb_set_rg35xx_keys(std::uint64_t keys)
{
    bridge_mapped_keys = keys;
}

extern "C" void trs_xlate_keysym(int symbol)
{
    bridge_guest_events.push_back(symbol);
}

extern "C" int trs_key_identity(int symbol)
{
    return symbol;
}

namespace {

int failures;

#define EXPECT(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: EXPECT(%s) failed\n", \
                     __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

struct SinkEvent {
    bool down;
    int symbol;
};

class FakeSink : public GuestKeySink {
public:
    FakeSink() : mapped_keys(0) {}
    void keyDown(int symbol) override { events.push_back({true, symbol}); }
    void keyUp(int symbol) override { events.push_back({false, symbol}); }
    void setMappedKeys(std::uint64_t keys) override { mapped_keys = keys; }
    std::uint64_t mapped_keys;
    std::vector<SinkEvent> events;
};

class CanonicalFakeSink : public FakeSink {
public:
    int keyIdentity(int symbol) const override
    {
        if (symbol >= 'A' && symbol <= 'Z')
            return symbol + ('a' - 'A');
        return symbol;
    }
};

class FakeControls : public RG35XXControls {
public:
    bool handleSDLEvent(const SDL_Event&) override { return false; }
    void update(std::uint32_t) override {}
    bool popEvent(ControlEvent& event) override
    {
        if (events.empty()) return false;
        event = events.front();
        events.pop_front();
        return true;
    }
    const ControlState& state() const override { return current; }
    void releaseAll(std::uint32_t now) override
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(Button::Count); ++i)
            if (current.buttons[i]) button(static_cast<Button>(i), false, now);
        for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i)
            if (current.axes[i] != 0.0f) axis(static_cast<Axis>(i), 0.0f, now);
    }
    void button(Button button_id, bool down, std::uint32_t now)
    {
        current.buttons[static_cast<std::size_t>(button_id)] = down;
        events.push_back(ControlEvent::buttonEvent(
            down ? ControlEventType::ButtonDown : ControlEventType::ButtonUp,
            button_id, now));
    }
    void axis(Axis axis_id, float value, std::uint32_t now)
    {
        current.axes[static_cast<std::size_t>(axis_id)] = value;
        events.push_back(ControlEvent::axisEvent(axis_id, value, now));
    }

    ControlState current;
    std::deque<ControlEvent> events;
};

class JoystickTranslatorControls : public RG35XXControls {
public:
    JoystickTranslatorControls(const SDLJoystickMapping& mapping,
                               SDL_JoystickID instance)
        : translator(mapping), instance_id(instance) {}
    bool handleSDLEvent(const SDL_Event& event) override
    {
        return translator.handleSDLEvent(event, instance_id);
    }
    void update(std::uint32_t) override {}
    bool popEvent(ControlEvent& event) override
    {
        return translator.popEvent(event);
    }
    const ControlState& state() const override { return translator.state(); }
    void releaseAll(std::uint32_t now) override { translator.releaseAll(now); }

    SDLJoystickEventTranslator translator;
    SDL_JoystickID instance_id;
};

SDL_Event keyEvent(SDL_Scancode scancode, bool down, std::uint32_t timestamp,
                   bool repeat = false)
{
    SDL_Event event = {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.type = event.type;
    event.key.timestamp = timestamp;
    event.key.repeat = repeat ? 1 : 0;
    event.key.keysym.scancode = scancode;
    /* The backend deliberately keys on physical scancodes only. */
    event.key.keysym.sym = SDLK_UNKNOWN;
    return event;
}

SDL_Event joyButtonEvent(SDL_JoystickID instance, Uint8 button, bool down,
                         std::uint32_t timestamp)
{
    SDL_Event event = {};
    event.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
    event.jbutton.type = event.type;
    event.jbutton.timestamp = timestamp;
    event.jbutton.which = instance;
    event.jbutton.button = button;
    event.jbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
    return event;
}

SDL_Event joyAxisEvent(SDL_JoystickID instance, Uint8 axis, Sint16 value,
                       std::uint32_t timestamp)
{
    SDL_Event event = {};
    event.type = SDL_JOYAXISMOTION;
    event.jaxis.type = event.type;
    event.jaxis.timestamp = timestamp;
    event.jaxis.which = instance;
    event.jaxis.axis = axis;
    event.jaxis.value = value;
    return event;
}

SDL_Event joyHatEvent(SDL_JoystickID instance, Uint8 hat, Uint8 value,
                      std::uint32_t timestamp)
{
    SDL_Event event = {};
    event.type = SDL_JOYHATMOTION;
    event.jhat.type = event.type;
    event.jhat.timestamp = timestamp;
    event.jhat.which = instance;
    event.jhat.hat = hat;
    event.jhat.value = value;
    return event;
}

SDLJoystickMapping testJoystickMapping()
{
    SDLJoystickMapping mapping;
    std::string error;
    EXPECT(SDLJoystickMapping::parse(
        "a=b0,b=b1,x=b2,y=b3,up=h0u,down=h0d,left=h0l,right=h0r,"
        "start=b4,select=b5,menu=b6,l1=b7,r1=b8,l2=b9,r2=b10,"
        "ls=b11,rs=b12,lx=a0,ly=-a1,rx=a2,ry=a3",
        mapping, error));
    EXPECT(error.empty());
    return mapping;
}

void send(RG35XXInputRouter& router, SDL_Scancode scancode, bool down,
          std::uint32_t now, bool repeat = false)
{
    SDL_Event event = keyEvent(scancode, down, now, repeat);
    EXPECT(router.handleSDLEvent(event, now));
}

void tap(RG35XXInputRouter& router, SDL_Scancode scancode, std::uint32_t& now)
{
    send(router, scancode, true, now++);
    send(router, scancode, false, now++);
}

void openOverlay(RG35XXInputRouter& router, std::uint32_t& now)
{
    tap(router, SDL_SCANCODE_ESCAPE, now);
    EXPECT(router.visible());
}

void openOverlay(FakeControls& controls, RG35XXInputRouter& router,
                 std::uint32_t now)
{
    controls.button(Button::Select, true, now);
    router.update(now);
    controls.button(Button::Select, false, now + 1);
    router.update(now + 1);
    EXPECT(router.visible());
}

void testSystemVolumeButtons()
{
    class Mixer : public VolumeMixer {
    public:
        Mixer() : level(10), changes(0) {}
        bool adjust(int direction) override {
            level = nextVolumeLevel(level, 0, 31, 1, direction);
            ++changes;
            return true;
        }
        long level;
        int changes;
    } mixer;
    {
        VolumeButtons buttons(mixer);
        EXPECT(mixer.changes == 0); // Starting the emulator preserves system volume.
        buttons.key(true, true, 100);
        EXPECT(mixer.level == 11 && mixer.changes == 1);
        buttons.key(true, true, 101); // Duplicate/kernel repeat is not a second press.
        buttons.update(449);
        EXPECT(mixer.changes == 1);
        buttons.update(450);
        EXPECT(mixer.level == 12 && mixer.changes == 2);
        mixer.level = 20; // An external system-volume change must be respected.
        buttons.update(550);
        EXPECT(mixer.level == 21);
        buttons.update(10000);
        EXPECT(mixer.level == 22); // No burst of delayed repeats.
        buttons.key(true, false, 10001);
        buttons.update(11000);
        EXPECT(mixer.level == 22);
        buttons.key(false, true, 11001);
        EXPECT(mixer.level == 21);
        buttons.key(true, true, 11002);
        buttons.update(12000); // Opposing buttons cancel.
        EXPECT(mixer.level == 21);
        buttons.release(); // Focus loss/disconnect stops repeat.
        buttons.update(13000);
        EXPECT(mixer.level == 21);
        buttons.key(false, true, 0xfffffff0u);
        EXPECT(mixer.level == 20);
        buttons.update(333u);
        EXPECT(mixer.level == 20);
        buttons.update(334u);
        EXPECT(mixer.level == 19);
    }
    EXPECT(mixer.level == 19); // Exit does not restore the previous volume.
    const long stock_levels[] = {0, 5, 10, 16, 19, 21, 23, 25, 27, 29, 31};
    for (int i = 0; i <= 10; ++i) EXPECT(stockVolumeMixerLevel(i) == stock_levels[i]);
    EXPECT(stockVolumeMixerLevel(-1) == 0);
    EXPECT(stockVolumeMixerLevel(11) == 31);
    EXPECT(nextVolumeLevel(10, 0, 10, 1, 1) == 10);
    EXPECT(nextVolumeLevel(0, 0, 10, 1, -1) == 0);
    EXPECT(nextVolumeLevel(31, 0, 31, 1, 1) == 31);
    EXPECT(nextVolumeLevel(0, 0, 31, 1, -1) == 0);
    EXPECT(nextVolumeLevel(30, 0, 31, 2, 1) == 31);
    EXPECT(nextVolumeLevel(1, 0, 31, 2, -1) == 0);
    EXPECT(nextVolumeLevel(10, 0, 31, 0, 1) == 11);
    EXPECT(nextVolumeLevel(10, 0, 31, 0, -1) == 9);
}

void testDefaultBindings()
{
    std::size_t count = 0;
    const HostBinding* bindings = KeyboardRG35XXControls::defaultBindings(count);
    EXPECT(count == 25);
    std::set<int> scancodes;
    for (std::size_t i = 0; i < count; ++i)
        EXPECT(scancodes.insert(bindings[i].scancode).second);

    const SDL_Scancode expected[] = {
        SDL_SCANCODE_F6, SDL_SCANCODE_SPACE, SDL_SCANCODE_TAB,
        SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
        SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_W,
        SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_I,
        SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_Q,
        SDL_SCANCODE_E, SDL_SCANCODE_1, SDL_SCANCODE_3, SDL_SCANCODE_C,
        SDL_SCANCODE_V, SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE,
        SDL_SCANCODE_F5
    };
    for (std::size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        EXPECT(scancodes.count(expected[i]) == 1);

    KeyboardRG35XXControls controls;
    SDL_Event event = keyEvent(SDL_SCANCODE_F6, true, 1);
    EXPECT(controls.handleSDLEvent(event));
    event = keyEvent(SDL_SCANCODE_F6, false, 2);
    EXPECT(controls.handleSDLEvent(event));
    ControlEvent logical;
    EXPECT(controls.popEvent(logical));
    EXPECT(logical.type == ControlEventType::ButtonDown);
    EXPECT(logical.button == Button::A);
    EXPECT(controls.popEvent(logical));
    EXPECT(logical.type == ControlEventType::ButtonUp);
    EXPECT(!controls.popEvent(logical));

    event = keyEvent(SDL_SCANCODE_A, true, 3);
    controls.handleSDLEvent(event);
    EXPECT(controls.state().value(Axis::LeftX) == -1.0f);
    event = keyEvent(SDL_SCANCODE_D, true, 4);
    controls.handleSDLEvent(event);
    EXPECT(controls.state().value(Axis::LeftX) == 0.0f);
    event = keyEvent(SDL_SCANCODE_A, false, 5);
    controls.handleSDLEvent(event);
    EXPECT(controls.state().value(Axis::LeftX) == 1.0f);
    event = keyEvent(SDL_SCANCODE_D, false, 6);
    controls.handleSDLEvent(event);
    EXPECT(controls.state().value(Axis::LeftX) == 0.0f);

    event = keyEvent(SDL_SCANCODE_I, true, 7);
    controls.handleSDLEvent(event);
    event = keyEvent(SDL_SCANCODE_J, true, 8);
    controls.handleSDLEvent(event);
    EXPECT(controls.state().value(Axis::RightY) == -1.0f);
    EXPECT(controls.state().value(Axis::RightX) == -1.0f);
    event = keyEvent(SDL_SCANCODE_I, false, 9);
    controls.handleSDLEvent(event);
    event = keyEvent(SDL_SCANCODE_J, false, 10);
    controls.handleSDLEvent(event);

    event = keyEvent(SDL_SCANCODE_F6, true, 11, true);
    EXPECT(controls.handleSDLEvent(event));
    EXPECT(!controls.state().pressed(Button::A));
    event = keyEvent(SDL_SCANCODE_Z, true, 12);
    EXPECT(!controls.handleSDLEvent(event));
}

void testSDLJoystickMappingAndTranslation()
{
    const SDLJoystickMapping mapping = testJoystickMapping();
    std::string error;
    SDLJoystickMapping invalid;
    EXPECT(!SDLJoystickMapping::parse("b=b1,lx=a0,ly=a1", invalid, error));
    EXPECT(error.find("A and B") != std::string::npos);
    EXPECT(!SDLJoystickMapping::parse(
        "a=b0,b=b0,lx=a0,ly=a1", invalid, error));
    EXPECT(error.find("mapped twice") != std::string::npos);
    EXPECT(!SDLJoystickMapping::parse(
        "a=b0,b=b1,up=h0q,down=h0d,left=h0l,right=h0r",
        invalid, error));
    EXPECT(error.find("invalid hat") != std::string::npos);

    SDLJoystickEventTranslator translator(mapping);
    const SDL_JoystickID instance = 42;
    SDL_Event event = joyButtonEvent(instance + 1, 0, true, 1);
    EXPECT(!translator.handleSDLEvent(event, instance));

    event = joyButtonEvent(instance, 0, true, 2);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(translator.state().pressed(Button::A));
    event = joyButtonEvent(instance, 0, false, 3);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(!translator.state().pressed(Button::A));
    ControlEvent logical;
    EXPECT(translator.popEvent(logical));
    EXPECT(logical.type == ControlEventType::ButtonDown &&
           logical.button == Button::A);
    EXPECT(translator.popEvent(logical));
    EXPECT(logical.type == ControlEventType::ButtonUp &&
           logical.button == Button::A);

    event = joyAxisEvent(instance, 0, 16384, 4);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(translator.state().value(Axis::LeftX) > 0.49f &&
           translator.state().value(Axis::LeftX) < 0.51f);
    event = joyAxisEvent(instance, 1, -32768, 5);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(translator.state().value(Axis::LeftY) == 1.0f);

    event = joyHatEvent(instance, 0, SDL_HAT_RIGHTUP, 6);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(translator.state().pressed(Button::DPadUp));
    EXPECT(translator.state().pressed(Button::DPadRight));
    EXPECT(!translator.state().pressed(Button::DPadDown));
    event = joyHatEvent(instance, 0, SDL_HAT_CENTERED, 7);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(!translator.state().pressed(Button::DPadUp));
    EXPECT(!translator.state().pressed(Button::DPadRight));

    while (translator.popEvent(logical)) {}
    event = joyButtonEvent(instance, 1, true, 8);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(translator.state().pressed(Button::B));
    translator.releaseAll(9);
    EXPECT(!translator.state().pressed(Button::B));
    event = joyButtonEvent(instance, 1, true, 10);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(!translator.state().pressed(Button::B));
    event = joyButtonEvent(instance, 1, false, 11);
    EXPECT(translator.handleSDLEvent(event, instance));
    event = joyButtonEvent(instance, 1, true, 12);
    EXPECT(translator.handleSDLEvent(event, instance));
    EXPECT(translator.state().pressed(Button::B));
}

void testPhysicalPassthroughPolicy()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard, false);

    SDL_Event event = keyEvent(SDL_SCANCODE_E, true, 1); // R1 has no guest default.
    EXPECT(!router.handleSDLEvent(event, 1));
    send(router, SDL_SCANCODE_ESCAPE, true, 2);
    EXPECT(!router.visible());
    send(router, SDL_SCANCODE_ESCAPE, false, 3);
    EXPECT(router.visible());
    event = keyEvent(SDL_SCANCODE_E, false, 4);
    EXPECT(!router.handleSDLEvent(event, 4)); // Preserve a passthrough release.
    EXPECT(sink.events.empty());
    send(router, SDL_SCANCODE_E, true, 5);
    send(router, SDL_SCANCODE_ESCAPE, true, 6);
    send(router, SDL_SCANCODE_ESCAPE, false, 7);
    EXPECT(!router.visible());
    send(router, SDL_SCANCODE_E, false, 8); // Preserve a captured release.
    event = keyEvent(SDL_SCANCODE_E, true, 9);
    EXPECT(!router.handleSDLEvent(event, 9));
    event = keyEvent(SDL_SCANCODE_E, false, 10);
    EXPECT(!router.handleSDLEvent(event, 10));
    send(router, SDL_SCANCODE_A, true, 11);
    send(router, SDL_SCANCODE_ESCAPE, true, 12);
    send(router, SDL_SCANCODE_ESCAPE, false, 13);
    send(router, SDL_SCANCODE_A, false, 14);
}

void testPhysicalHatCaptureAndGating()
{
    JoystickTranslatorControls controls(testJoystickMapping(), 42);
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard, false);

    SDL_Event event = joyHatEvent(42, 0, SDL_HAT_UP, 1);
    EXPECT(router.handleSDLEvent(event, 1));
    EXPECT(sink.events.size() == 1 && sink.events[0].symbol == 0x111);
    event = joyButtonEvent(42, 5, true, 2);
    EXPECT(router.handleSDLEvent(event, 2));
    EXPECT(!router.visible());
    event = joyButtonEvent(42, 5, false, 3);
    EXPECT(router.handleSDLEvent(event, 3));
    EXPECT(router.visible());
    EXPECT(sink.events.size() == 2 && !sink.events[1].down);
    const int initial = router.softKeyboard().selection();

    event = joyHatEvent(42, 0, SDL_HAT_RIGHTUP, 4);
    EXPECT(router.handleSDLEvent(event, 4));
    EXPECT(router.softKeyboard().selection() == initial + 1);
    event = joyHatEvent(42, 0, SDL_HAT_RIGHT, 5);
    EXPECT(router.handleSDLEvent(event, 5));
    EXPECT(router.softKeyboard().selection() == initial + 1);
    event = joyHatEvent(42, 0, SDL_HAT_CENTERED, 6);
    EXPECT(router.handleSDLEvent(event, 6));
    EXPECT(router.softKeyboard().selection() == initial + 1);

    event = joyHatEvent(42, 0, SDL_HAT_RIGHT, 7);
    EXPECT(router.handleSDLEvent(event, 7));
    EXPECT(router.softKeyboard().selection() == initial + 2);
    event = joyHatEvent(42, 0, SDL_HAT_CENTERED, 8);
    EXPECT(router.handleSDLEvent(event, 8));
}

void testDefaultGuestBindings()
{
    FakeControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 1;
    const auto button = [&](Button id, bool down) {
        controls.button(id, down, now);
        router.update(now++);
    };
    const auto axis = [&](Axis id, float value) {
        controls.axis(id, value, now);
        router.update(now++);
    };
    const Button buttons[] = {Button::DPadUp, Button::DPadDown,
        Button::DPadLeft, Button::DPadRight, Button::RightStick, Button::A, Button::B, Button::X};
    const int symbols[] = {0x111, 0x112, 0x114, 0x113, ' ', 13, 0x1b, 0x116};
    for (std::size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        sink.events.clear();
        button(buttons[i], true);
        router.update(now + 1000); // Held guest keys do not generate duplicate downs.
        EXPECT(sink.events.size() == 1);
        button(buttons[i], false);
        EXPECT(sink.events.size() == 2);
        EXPECT(sink.events[0].down && sink.events[0].symbol == symbols[i]);
        EXPECT(!sink.events[1].down && sink.events[1].symbol == symbols[i]);
    }
    const Button shoulders[] = {Button::L1, Button::L2, Button::R1, Button::R2, Button::Start};
    sink.events.clear();
    for (std::size_t i = 0; i < sizeof(shoulders) / sizeof(shoulders[0]); ++i) {
        button(shoulders[i], true);
        button(shoulders[i], false);
        EXPECT(sink.events.empty());
    }
    sink.events.clear();
    button(Button::R1, true);
    button(Button::R2, true);
    button(Button::RightStick, true);
    EXPECT(keyboard.press(GuestOwnerPhysicalBase, ' '));
    EXPECT(sink.events.size() == 1);
    button(Button::R1, false);
    button(Button::R2, false);
    button(Button::RightStick, false);
    EXPECT(sink.events.size() == 1);
    keyboard.release(GuestOwnerPhysicalBase);
    EXPECT(sink.events.size() == 2 && !sink.events.back().down);

    sink.events.clear();
    axis(Axis::LeftX, -0.54f);
    EXPECT(sink.events.empty());
    axis(Axis::LeftX, -0.60f);
    axis(Axis::LeftY, -0.80f);
    EXPECT(sink.events.size() == 2);
    EXPECT(sink.events[0].symbol == 0x114 && sink.events[1].symbol == 0x111);
    button(Button::DPadLeft, true);
    axis(Axis::LeftX, -0.40f);
    EXPECT(sink.events.size() == 2);
    axis(Axis::LeftX, 0.0f);
    EXPECT(sink.events.size() == 2); // D-pad still owns LEFT.
    button(Button::DPadLeft, false);
    EXPECT(sink.events.size() == 3 && !sink.events.back().down);
    axis(Axis::LeftY, 0.90f);
    EXPECT(sink.events.size() == 5);
    EXPECT(!sink.events[3].down && sink.events[3].symbol == 0x111);
    EXPECT(sink.events[4].down && sink.events[4].symbol == 0x112);
    axis(Axis::LeftY, 0.0f);
    EXPECT(sink.events.size() == 6 && !sink.events.back().down);

    sink.events.clear();
    button(Button::A, true);
    button(Button::Select, true);
    EXPECT(!router.visible());
    button(Button::Select, false);
    EXPECT(router.visible());
    EXPECT(sink.events.size() == 2 && !sink.events.back().down);
    button(Button::Select, true);
    button(Button::Select, false);
    EXPECT(!router.visible());
    button(Button::B, true);
    EXPECT(sink.events.size() == 3 && sink.events.back().symbol == 0x1b);
    button(Button::A, false); // Held before keyboard entry remains gated.
    EXPECT(sink.events.size() == 3);
    button(Button::A, true);
    EXPECT(sink.events.size() == 4 && sink.events.back().symbol == 0x0d);
    router.lifecycleCleanup(now++);
    EXPECT(sink.events.size() == 6);
    button(Button::A, false);
    button(Button::B, false);
    EXPECT(sink.events.size() == 6);

    JoystickTranslatorControls physical(testJoystickMapping(), 42);
    RG35XXInputRouter physical_router(physical, keyboard, false);
    sink.events.clear();
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(42, 0, true, now), now));
    EXPECT(physical_router.handleSDLEvent(joyAxisEvent(42, 0, 30000, now), now));
    EXPECT(sink.events.size() == 2);
    physical.releaseAll(++now); // Device removal must release the default keys.
    physical_router.update(now);
    EXPECT(sink.events.size() == 4);
    EXPECT(!sink.events[2].down && !sink.events[3].down);
}

void testToggleAndEntryGating()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 100;

    send(router, SDL_SCANCODE_SPACE, true, now++);
    send(router, SDL_SCANCODE_ESCAPE, true, now++);
    EXPECT(!router.visible());
    send(router, SDL_SCANCODE_ESCAPE, true, now++, true);
    EXPECT(!router.visible());
    send(router, SDL_SCANCODE_ESCAPE, false, now++);
    EXPECT(router.visible());
    send(router, SDL_SCANCODE_SPACE, false, now++);
    EXPECT(sink.events.size() == 2);

    send(router, SDL_SCANCODE_ESCAPE, true, now++);
    EXPECT(router.visible());
    send(router, SDL_SCANCODE_ESCAPE, false, now++);
    EXPECT(!router.visible());
    EXPECT(sink.events.size() == 2);

    SDL_Event quit = {};
    quit.type = SDL_QUIT;
    EXPECT(!router.handleSDLEvent(quit, now));
    SDL_Event window = {};
    window.type = SDL_WINDOWEVENT;
    window.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    EXPECT(!router.handleSDLEvent(window, now));
}

std::set<int> visitAllKeys(bool use_stick)
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 1000;
    openOverlay(router, now);
    const SDL_Scancode up = use_stick ? SDL_SCANCODE_W : SDL_SCANCODE_UP;
    const SDL_Scancode down = use_stick ? SDL_SCANCODE_S : SDL_SCANCODE_DOWN;
    const SDL_Scancode left = use_stick ? SDL_SCANCODE_A : SDL_SCANCODE_LEFT;
    const SDL_Scancode right = use_stick ? SDL_SCANCODE_D : SDL_SCANCODE_RIGHT;
    for (int i = 0; i < 5; ++i) tap(router, up, now);
    for (int i = 0; i < 15; ++i) tap(router, left, now);

    std::set<int> visited;
    bool moving_right = true;
    for (int row = 0; row < 4; ++row) {
        visited.insert(router.softKeyboard().selection());
        for (int col = 1; col < 13; ++col) {
            tap(router, moving_right ? right : left, now);
            visited.insert(router.softKeyboard().selection());
        }
        if (row != 3) {
            tap(router, down, now);
            visited.insert(router.softKeyboard().selection());
        }
        moving_right = !moving_right;
    }
    return visited;
}

void testNavigationAndRepeat()
{
    EXPECT(visitAllKeys(false).size() == 52);
    EXPECT(visitAllKeys(true).size() == 52);

    FakeControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    openOverlay(controls, router, 10);
    const int initial = router.softKeyboard().selection();

    controls.axis(Axis::LeftX, 0.54f, 20);
    router.update(20);
    EXPECT(router.softKeyboard().selection() == initial);
    controls.axis(Axis::LeftX, 0.56f, 21);
    router.update(21);
    EXPECT(router.softKeyboard().selection() == initial + 1);
    const int after_engage = router.softKeyboard().selection();
    controls.axis(Axis::LeftX, 0.40f, 22);
    router.update(22);
    EXPECT(router.navigationDirection() == NavigationDirection::Right);
    controls.axis(Axis::LeftX, 0.34f, 23);
    router.update(23);
    EXPECT(router.navigationDirection() == NavigationDirection::None);
    controls.axis(Axis::LeftX, -0.60f, 24);
    router.update(24);
    EXPECT(router.softKeyboard().selection() == after_engage - 1);

    controls.axis(Axis::LeftX, 0.0f, 25);
    router.update(25);
    controls.button(Button::DPadRight, true, 30);
    router.update(30);
    const int after_immediate = router.softKeyboard().selection();
    router.update(329);
    EXPECT(router.softKeyboard().selection() == after_immediate);
    router.update(330);
    EXPECT(router.softKeyboard().selection() == after_immediate + 1);
    router.update(420);
    EXPECT(router.softKeyboard().selection() == after_immediate + 2);
    controls.button(Button::DPadRight, false, 421);
    router.update(421);

    controls.button(Button::DPadLeft, true, 430);
    router.update(430);
    controls.button(Button::DPadRight, true, 431);
    router.update(431);
    EXPECT(router.navigationDirection() == NavigationDirection::None);
    controls.button(Button::DPadLeft, false, 432);
    controls.button(Button::DPadRight, false, 432);
    router.update(432);

    controls.axis(Axis::LeftX, -0.9f, 440);
    router.update(440);
    controls.button(Button::DPadDown, true, 441);
    router.update(441);
    EXPECT(router.navigationDirection() == NavigationDirection::Down);
    controls.button(Button::DPadDown, false, 442);
    router.update(442);
    EXPECT(router.navigationDirection() == NavigationDirection::Left);
    controls.axis(Axis::LeftX, 0.0f, 443);
    router.update(443);

    controls.button(Button::DPadRight, true, 0xfffffff0u);
    router.update(0xfffffff0u);
    const int before_wrap_repeat = router.softKeyboard().selection();
    router.update(283u);
    EXPECT(router.softKeyboard().selection() == before_wrap_repeat);
    router.update(284u);
    EXPECT(router.softKeyboard().selection() == before_wrap_repeat + 1);
}

void testGuestKeysShiftAndOwnership()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 2000;
    openOverlay(router, now);

    tap(router, SDL_SCANCODE_SPACE, now);
    EXPECT(sink.events.size() == 2);
    EXPECT(sink.events[0].down && sink.events[0].symbol == SDLK_q);
    EXPECT(!sink.events[1].down && sink.events[1].symbol == SDLK_q);

    sink.events.clear();
    tap(router, SDL_SCANCODE_TAB, now);
    EXPECT(router.softKeyboard().shiftLatched());
    send(router, SDL_SCANCODE_SPACE, true, now++);
    EXPECT(sink.events.size() == 1 && sink.events[0].symbol == 0x51);
    send(router, SDL_SCANCODE_RIGHT, true, now++);
    send(router, SDL_SCANCODE_RIGHT, false, now++);
    send(router, SDL_SCANCODE_SPACE, false, now++);
    EXPECT(sink.events.size() == 2);
    EXPECT(!sink.events[1].down && sink.events[1].symbol == 0x51);
    EXPECT(!router.softKeyboard().shiftLatched());

    sink.events.clear();
    router.softKeyboardForTesting().setSelection(39);
    tap(router, SDL_SCANCODE_SPACE, now);
    EXPECT(router.softKeyboard().shiftLatched());
    EXPECT(sink.events.empty());
    router.softKeyboardForTesting().setSelection(14);

    send(router, SDL_SCANCODE_BACKSPACE, true, now++);
    send(router, SDL_SCANCODE_BACKSPACE, false, now++);
    send(router, SDL_SCANCODE_F6, true, now++);
    send(router, SDL_SCANCODE_F6, false, now++);
    EXPECT(sink.events.size() == 4);
    EXPECT(sink.events[0].symbol == 0x114 && sink.events[0].down);
    EXPECT(sink.events[1].symbol == 0x114 && !sink.events[1].down);
    EXPECT(sink.events[2].symbol == 0x0d && sink.events[2].down);
    EXPECT(sink.events[3].symbol == 0x0d && !sink.events[3].down);

    sink.events.clear();
    EXPECT(keyboard.press(GuestOwnerPhysicalBase + 4, SDLK_q));
    EXPECT(keyboard.press(GuestOwnerOverlayB, SDLK_q));
    EXPECT(sink.events.size() == 1 && sink.events[0].down);
    EXPECT(keyboard.release(GuestOwnerOverlayB));
    EXPECT(sink.events.size() == 1);
    EXPECT(keyboard.release(GuestOwnerPhysicalBase + 4));
    EXPECT(sink.events.size() == 2 && !sink.events[1].down);
    EXPECT(!keyboard.press(77, -1));

    CanonicalFakeSink canonical_sink;
    TrsKeyboardAdapter canonical_keyboard(canonical_sink);
    EXPECT(canonical_keyboard.press(1, 'Q'));
    EXPECT(canonical_keyboard.press(2, 'q'));
    EXPECT(canonical_sink.events.size() == 1);
    EXPECT(canonical_keyboard.release(1));
    EXPECT(canonical_sink.events.size() == 1);
    EXPECT(canonical_keyboard.release(2));
    EXPECT(canonical_sink.events.size() == 2);
    EXPECT(canonical_sink.events[1].symbol == 'Q');
}

void testMenuControls()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 100;
    tap(router, SDL_SCANCODE_F5, now);
    EXPECT(router.popMenuKey() == SDLK_F7);
    EXPECT(router.popMenuKey() == 0);
    openOverlay(router, now);
    tap(router, SDL_SCANCODE_F5, now);
    EXPECT(!router.visible());
    EXPECT(router.popMenuKey() == SDLK_F7);
    router.setSuspended(true, now++);
    tap(router, SDL_SCANCODE_F5, now);
    EXPECT(router.popMenuKey() == SDLK_ESCAPE);
    tap(router, SDL_SCANCODE_F6, now);
    EXPECT(router.popMenuKey() == SDLK_RETURN);
    EXPECT(!router.visible());
    tap(router, SDL_SCANCODE_SPACE, now);
    EXPECT(router.popMenuKey() == SDLK_ESCAPE);
    tap(router, SDL_SCANCODE_RETURN, now);
    EXPECT(router.popMenuKey() == SDLK_RETURN);
    tap(router, SDL_SCANCODE_ESCAPE, now);
    EXPECT(router.popMenuKey() == SDLK_ESCAPE);
    send(router, SDL_SCANCODE_DOWN, true, now);
    EXPECT(router.popMenuKey() == SDLK_DOWN);
    router.update(now + 299);
    EXPECT(router.popMenuKey() == 0);
    router.update(now + 300);
    EXPECT(router.popMenuKey() == SDLK_DOWN);
    send(router, SDL_SCANCODE_DOWN, false, now + 301);
    now += 400;
    tap(router, SDL_SCANCODE_D, now);
    EXPECT(router.popMenuKey() == SDLK_RIGHT);
    EXPECT(sink.events.empty());
    router.setSuspended(false, now++);
    EXPECT(router.popMenuKey() == 0);

    JoystickTranslatorControls physical(testJoystickMapping(), 42);
    RG35XXInputRouter physical_router(physical, keyboard, false);
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(42, 6, true, now), now));
    EXPECT(physical_router.popMenuKey() == SDLK_F7);
    physical_router.setSuspended(true, ++now);
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(42, 6, false, now), now));
    EXPECT(physical_router.popMenuKey() == 0);
    EXPECT(physical_router.handleSDLEvent(joyHatEvent(42, 0, SDL_HAT_UP, now), now));
    EXPECT(physical_router.popMenuKey() == SDLK_UP);
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(99, 0, true, now), now) == false);
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(42, 0, true, now), now));
    EXPECT(physical_router.popMenuKey() == SDLK_RETURN);
    physical_router.setTextEntry(true, ++now);
    EXPECT(physical_router.visible());
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(42, 0, false, now), now));
    EXPECT(physical_router.handleSDLEvent(joyButtonEvent(42, 1, true, now), now));
    EXPECT(physical_router.popMenuKey() == 'q');
    EXPECT(sink.events.empty());
    physical_router.setTextEntry(false, ++now);
    physical_router.setSuspended(false, ++now);
    EXPECT(physical_router.popMenuKey() == 0);
    EXPECT(!physical_router.visible());
}

void testMenuTextEntry()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 1000;
    router.setSuspended(true, now++);
    // A opens this field from its parent menu; holding it must not close it.
    send(router, SDL_SCANCODE_F6, true, now++);
    EXPECT(router.popMenuKey() == SDLK_RETURN);
    router.setTextEntry(true, now++);
    EXPECT(router.visible());
    send(router, SDL_SCANCODE_F6, true, now++, true);
    EXPECT(router.visible());
    send(router, SDL_SCANCODE_F6, false, now++);
    EXPECT(router.popMenuKey() == 0);

    // Replace an RGB value with 12ABEF using the same keyboard as guest typing.
    tap(router, SDL_SCANCODE_1, now);
    EXPECT(router.popMenuKey() == SDLK_F9);
    const int selections[] = {0, 1, 27, 44, 16, 30};
    const char* expected = "12ABEF";
    for (int i = 0; i < 6; ++i) {
        router.softKeyboardForTesting().setSelection(selections[i]);
        if (i >= 2) tap(router, SDL_SCANCODE_TAB, now);
        tap(router, SDL_SCANCODE_SPACE, now);
        EXPECT(router.popMenuKey() == expected[i]);
        EXPECT(!router.softKeyboard().shiftLatched());
    }
    // Host text uses the printed punctuation, not guest German key encodings.
    router.softKeyboardForTesting().setSelection(24);
    tap(router, SDL_SCANCODE_TAB, now);
    tap(router, SDL_SCANCODE_SPACE, now);
    EXPECT(router.popMenuKey() == '[');
    tap(router, SDL_SCANCODE_BACKSPACE, now);
    EXPECT(router.popMenuKey() == SDLK_BACKSPACE);
    tap(router, SDL_SCANCODE_Q, now);
    EXPECT(router.popMenuKey() == SDLK_LEFT);
    tap(router, SDL_SCANCODE_E, now);
    EXPECT(router.popMenuKey() == SDLK_RIGHT);
    tap(router, SDL_SCANCODE_3, now);
    EXPECT(router.popMenuKey() == SDLK_TAB);

    const int before = router.softKeyboard().selection();
    send(router, SDL_SCANCODE_RIGHT, true, now);
    EXPECT(router.softKeyboard().selection() != before);
    EXPECT(router.popMenuKey() == 0);
    send(router, SDL_SCANCODE_RIGHT, false, ++now);
    tap(router, SDL_SCANCODE_RETURN, now);
    EXPECT(router.popMenuKey() == SDLK_RETURN);
    router.setTextEntry(false, now++);
    EXPECT(!router.visible());
    EXPECT(!router.textEntry());
    tap(router, SDL_SCANCODE_DOWN, now);
    EXPECT(router.popMenuKey() == SDLK_DOWN);

    router.setTextEntry(true, now++);
    tap(router, SDL_SCANCODE_F6, now);
    EXPECT(!router.visible());
    tap(router, SDL_SCANCODE_F6, now);
    EXPECT(router.visible());
    tap(router, SDL_SCANCODE_ESCAPE, now);
    EXPECT(router.popMenuKey() == SDLK_ESCAPE);
    router.setTextEntry(false, now++);
    router.setSuspended(false, now++);
    EXPECT(!router.visible());
    EXPECT(sink.events.empty());

    SoftKeyboard text_keyboard;
    text_keyboard.setSelection(51);
    EXPECT(text_keyboard.textSymbol() == ' ');
    text_keyboard.setSelection(38);
    EXPECT(text_keyboard.textSymbol() == SDLK_F9);
    text_keyboard.setSelection(37);
    EXPECT(text_keyboard.textSymbol() == SDLK_RETURN);
}

void testLifecycleCleanup()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 3000;
    openOverlay(router, now);
    send(router, SDL_SCANCODE_SPACE, true, now++);
    EXPECT(sink.events.size() == 1 && sink.events[0].down);
    router.lifecycleCleanup(now++);
    EXPECT(!router.visible());
    EXPECT(sink.events.size() == 2 && !sink.events[1].down);
    send(router, SDL_SCANCODE_SPACE, false, now++);
    EXPECT(sink.events.size() == 2);

    openOverlay(router, now);
    send(router, SDL_SCANCODE_F6, true, now++);
    router.setSuspended(true, now++);
    EXPECT(!router.visible());
    EXPECT(sink.events.back().symbol == 0x0d && !sink.events.back().down);
    router.setSuspended(false, now++);

    SDL_Event focus = {};
    focus.type = SDL_WINDOWEVENT;
    focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    EXPECT(!router.handleSDLEvent(focus, now++));
    EXPECT(!router.visible());

    send(router, SDL_SCANCODE_ESCAPE, true, now++);
    EXPECT(!router.visible()); // SELECT is pending until release.
    router.lifecycleCleanup(now++);
    send(router, SDL_SCANCODE_ESCAPE, false, now++);
    EXPECT(!router.visible()); // Cleanup cancels the pending keyboard tap.
    openOverlay(router, now);
}

void loadMapping(RG35XXInputRouter& router, const std::string& text,
                 std::uint32_t now = 1, bool valid = true)
{
    for (std::size_t i = 0; i < text.size(); ++i)
        EXPECT(router.mappingByte(static_cast<unsigned char>(text[i]), now));
    EXPECT(router.mappingByte(0, now) == valid);
}

void testControlMappingParserAndResolution()
{
    ControlMapping mapping;
    std::string error;
    EXPECT(ControlMapping::parse(
        "A:F,B:SPACE,SELECT+A:1,D_U:UP,D_L:LEFT,"
        "D_L+B:LEFT+SPACE,R1:LEFT+RIGHT,START:ENTER,SELECT+START:SHIFT+BREAK",
        mapping, error));
    EXPECT(error.empty());
    const std::uint32_t a = ControlMapping::buttonMask(Button::A);
    const std::uint32_t b = ControlMapping::buttonMask(Button::B);
    const std::uint32_t select = ControlMapping::buttonMask(Button::Select);
    const std::uint32_t up = ControlMapping::buttonMask(Button::DPadUp);
    const std::uint32_t left = ControlMapping::buttonMask(Button::DPadLeft);
    const std::uint64_t space_key = UINT64_C(1) << 55;
    EXPECT(mapping.resolve(a) == (UINT64_C(1) << 6)); // F, physical, no SHIFT.
    EXPECT(mapping.resolve(a | select) == (UINT64_C(1) << 33));
    EXPECT(mapping.resolve(b) == space_key);
    EXPECT(mapping.resolve(b | left | up) ==
           (space_key | (UINT64_C(1) << 53) | (UINT64_C(1) << 51)));
    EXPECT(mapping.resolve(ControlMapping::buttonMask(Button::R1)) ==
           ((UINT64_C(1) << 53) | (UINT64_C(1) << 54)));
    EXPECT(mapping.resolve(select | ControlMapping::buttonMask(Button::Start)) ==
           ((UINT64_C(1) << 56) | (UINT64_C(1) << 50)));
    EXPECT(mapping.resolve(select) == 0);

    const char* invalid[] = {"", "A", "A:", ":A", "A::A", "A:A,", ",A:A",
        "A:A,,B:B", "a:A", "MENU:A", "A:FIRE", "A:SPACE|F", "A:A++B",
        "A+A:B", "A:A+A", "A: A", "A:A\n", "A:ANY_KEY", "A:TEXT_INPUT",
        "A:NUMERIC_INPUT", "PROMPT(A):ANY_KEY", "OSK(SELECT,Q):Q",
        "RG1;B=SPACE", "RG1;A=1;B=2", "RG1;PROMPT(A)=ANY_KEY",
        "RG1;OSK(SELECT,TEXT_INPUT)=TEXT_INPUT"};
    for (std::size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        EXPECT(!ControlMapping::parse(invalid[i], mapping, error));
        EXPECT(!error.empty());
    }
    EXPECT(!ControlMapping::parse(std::string("A:A\0,B:B", 8), mapping, error));
    EXPECT(!ControlMapping::parse("A:" + std::string(4094, 'A'), mapping, error));

    // Every documented physical key maps to exactly one unique matrix bit.
    const char* names[] = {"@", "ENTER", "CLEAR", "BREAK", "SPACE", "UP",
        "DOWN", "LEFT", "RIGHT", "COMMA", "PERIOD", "SEMICOLON", "SHIFT"};
    std::uint64_t seen = 0;
    for (int i = 0; i < 49; ++i) {
        std::string key;
        if (i < 26) key += static_cast<char>('A' + i);
        else if (i < 36) key += static_cast<char>('0' + i - 26);
        else key = names[i - 36];
        EXPECT(ControlMapping::parse("B:" + key, mapping, error));
        const std::uint64_t output = mapping.resolve(b);
        EXPECT(output && !(output & (output - 1)) && !(seen & output));
        seen |= output;
    }
}

void testControlMappingRoutingAndReceiver()
{
    FakeControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    const std::uint64_t space = UINT64_C(1) << 55;
    const std::uint64_t one = UINT64_C(1) << 33;
    const std::uint64_t left = UINT64_C(1) << 53;
    loadMapping(router, "A:SPACE,B:SPACE,SELECT+A:1,D_L:LEFT,"
                "LS_U:A,RS_D:PERIOD,LS_U+RS_D+R1:A+PERIOD+SPACE");
    EXPECT(router.mappingActive());
    controls.button(Button::A, true, 2); router.update(2);
    EXPECT(!router.visible() && sink.mapped_keys == space);
    EXPECT(sink.events.empty()); // No default controls or ASCII translation.
    controls.button(Button::B, true, 3); router.update(3);
    controls.button(Button::A, false, 4); router.update(4);
    EXPECT(sink.mapped_keys == space);
    controls.button(Button::A, true, 5);
    controls.button(Button::Select, true, 5);
    controls.button(Button::DPadLeft, true, 5); router.update(5);
    EXPECT(sink.mapped_keys == (one | space | left)); // Independent B and direction survive.
    controls.button(Button::B, false, 6); router.update(6);
    EXPECT(sink.mapped_keys == (one | left));
    controls.button(Button::Select, false, 7); router.update(7);
    EXPECT(sink.mapped_keys == (space | left));

    // Partial and invalid frames preserve both the mapping and held keys.
    router.mappingByte('R', 8);
    EXPECT(sink.mapped_keys == (space | left));
    EXPECT(!router.mappingByte(0, 8));
    loadMapping(router, "B:BOGUS", 9, false);
    EXPECT(sink.mapped_keys == (space | left));
    loadMapping(router, "B:" + std::string(5000, 'A'), 10, false);
    EXPECT(sink.mapped_keys == (space | left));

    // Removed RG1 syntax cannot replace a valid compact mapping or release it.
    loadMapping(router, "RG1;A=1;B=2", 10, false);
    EXPECT(sink.mapped_keys == (space | left));

    router.lifecycleCleanup(11);
    EXPECT(sink.mapped_keys == 0 && router.mappingActive());
    controls.axis(Axis::LeftY, -0.54f, 12); router.update(12);
    EXPECT(sink.mapped_keys == 0);
    controls.axis(Axis::LeftY, -0.56f, 13); router.update(13);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 1));
    controls.axis(Axis::RightY, 0.9f, 14);
    controls.button(Button::R1, true, 14); router.update(14);
    EXPECT(sink.mapped_keys == ((UINT64_C(1) << 1) | (UINT64_C(1) << 46) | space));
    controls.axis(Axis::LeftY, -0.4f, 15); router.update(15);
    EXPECT(sink.mapped_keys & space); // Hysteresis holds the larger chord.
    controls.axis(Axis::LeftY, -0.3f, 16); router.update(16);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 46));
    router.setSuspended(true, 17);
    EXPECT(sink.mapped_keys == 0);
    controls.button(Button::A, true, 18); router.update(18);
    EXPECT(router.popMenuKey() == SDLK_RETURN && sink.mapped_keys == 0);
    router.setSuspended(false, 19);
    controls.button(Button::A, false, 20); router.update(20);
    controls.button(Button::A, true, 21); router.update(21);
    EXPECT(sink.mapped_keys == space);

    loadMapping(router, "A:1", 22);
    EXPECT(sink.mapped_keys == 0); // Replacement gates held controls.
    router.update(23); EXPECT(sink.mapped_keys == 0);
    controls.button(Button::A, false, 24); router.update(24);
    controls.button(Button::A, true, 25); router.update(25);
    EXPECT(sink.mapped_keys == one);
    loadMapping(router, "", 26);
    EXPECT(!router.mappingActive() && sink.mapped_keys == 0);
    controls.button(Button::A, false, 27); router.update(27);
    controls.button(Button::A, true, 28); router.update(28);
    EXPECT(!router.visible()); // Empty frames restore A to ENTER.
    EXPECT(keyboard.ownerSymbol(GuestOwnerDefaultButtonBase + static_cast<int>(Button::A)) == 0x0d);
    controls.button(Button::Select, true, 28); router.update(28);
    controls.button(Button::Select, false, 28); router.update(28);
    EXPECT(router.visible());

    // Maximum legal frame, then immediate recovery after oversized reception.
    std::string longest;
    const char* buttons[] = {"A", "B", "X", "Y", "L1", "L2", "R1", "R2", "START"};
    for (unsigned int mask = 1; mask < 512; ++mask) {
        std::string trigger;
        for (unsigned int bit = 0; bit < 9; ++bit) {
            if (!(mask & (1u << bit))) continue;
            if (!trigger.empty()) trigger += "+";
            trigger += buttons[bit];
        }
        const std::string binding = (longest.empty() ? "" : ",") + trigger + ":A";
        if (longest.size() + binding.size() > ControlMapping::kMaxLength) break;
        longest += binding;
    }
    // Fill the last key chord with unique physical keys to reach exactly 4095.
    if ((ControlMapping::kMaxLength - longest.size()) % 2)
        longest.replace(longest.size() - 1, 1, "UP");
    const char* padding_keys = "0123456789BCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (std::size_t i = 0; longest.size() < ControlMapping::kMaxLength; ++i) {
        longest += "+";
        longest += padding_keys[i];
    }
    EXPECT(longest.size() == ControlMapping::kMaxLength);
    loadMapping(router, longest, 29);
    EXPECT(router.mappingActive() && !router.visible());
    loadMapping(router, longest + "A", 30, false);
    loadMapping(router, "B:2", 31);
    controls.button(Button::B, true, 32); router.update(32);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 34));
}

void testControlMappingPhysicalConsumption()
{
    JoystickTranslatorControls controls(testJoystickMapping(), 42);
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard, false);
    loadMapping(router, "B:SPACE,RS_D:9,D_U+D_R:SHIFT+BREAK");
    EXPECT(router.handleSDLEvent(joyButtonEvent(42, 1, true, 1), 1));
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 55));
    EXPECT(router.handleSDLEvent(joyAxisEvent(42, 3, 32767, 2), 2));
    EXPECT(sink.mapped_keys == ((UINT64_C(1) << 55) | (UINT64_C(1) << 41)));
    EXPECT(router.handleSDLEvent(joyHatEvent(42, 0, SDL_HAT_RIGHTUP, 3), 3));
    EXPECT(sink.mapped_keys == ((UINT64_C(1) << 55) | (UINT64_C(1) << 41) |
                              (UINT64_C(1) << 56) | (UINT64_C(1) << 50)));
    EXPECT(!router.handleSDLEvent(joyButtonEvent(43, 1, false, 4), 4));
    EXPECT(router.handleSDLEvent(joyHatEvent(42, 0, SDL_HAT_CENTERED, 5), 5));
    router.lifecycleCleanup(6);
    EXPECT(sink.mapped_keys == 0);
}

void testCompactMappings()
{
    ControlMapping mapping;
    std::string error;
    EXPECT(ControlMapping::parse("L1:1,L2:2,Y:D", mapping, error));
    EXPECT(mapping.resolve(ControlMapping::buttonMask(Button::L1)) == (UINT64_C(1) << 33));
    EXPECT(mapping.resolve(ControlMapping::buttonMask(Button::L2)) == (UINT64_C(1) << 34));
    EXPECT(mapping.resolve(ControlMapping::buttonMask(Button::Y)) == (UINT64_C(1) << 4));
    EXPECT(ControlMapping::parse("A:SPACE,SELECT+A:1,D_L:LEFT,"
                             "LS_U+RS_D+R1:A+PERIOD+SPACE", mapping, error));
    EXPECT(mapping.resolve(ControlMapping::buttonMask(Button::A) |
                           ControlMapping::buttonMask(Button::Select) |
                           ControlMapping::buttonMask(Button::DPadLeft)) ==
           ((UINT64_C(1) << 33) | (UINT64_C(1) << 53)));
    EXPECT(mapping.resolve(ControlMapping::axisMask(Axis::LeftY, -1) |
                           ControlMapping::axisMask(Axis::RightY, 1) |
                           ControlMapping::buttonMask(Button::R1)) ==
           ((UINT64_C(1) << 1) | (UINT64_C(1) << 46) | (UINT64_C(1) << 55)));
    const char* invalid[] = {"A", "A:", ":A", "A:A,", ",A:A", "A:A,,B:B",
        "A::A", "A:A|B", "A:A;B:B", "a:A", "A:a", "MENU:A", "A:ANY_KEY",
        "A:TEXT_INPUT", "A:NUMERIC_INPUT", "A+B:1,B+A:2", "A:1,A:2", "A:A+B+B"};
    for (std::size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        EXPECT(!ControlMapping::parse(invalid[i], mapping, error));

    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 100;
    loadMapping(router, "L1:1,L2:2,Y:D", now++);
    send(router, SDL_SCANCODE_Q, true, now++);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 33));
    send(router, SDL_SCANCODE_Q, false, now++);
    send(router, SDL_SCANCODE_1, true, now++);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 34));
    send(router, SDL_SCANCODE_1, false, now++);
    send(router, SDL_SCANCODE_BACKSPACE, true, now++);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 4));
    loadMapping(router, "A:BOGUS", now++, false);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 4));
    loadMapping(router, "", now++);
    EXPECT(sink.mapped_keys == 0 && !router.mappingActive());
}

void testCustomMappingsAugmentDefaults()
{
    FakeControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 100;
    const auto owner = [](Button button) {
        return GuestOwnerDefaultButtonBase + static_cast<int>(button);
    };
    const auto button = [&](Button id, bool down) {
        controls.button(id, down, now);
        router.update(now++);
    };
    const auto axis = [&](Axis id, float value) {
        controls.axis(id, value, now);
        router.update(now++);
    };

    loadMapping(router, "L1:1,L2:2,Y:D", now++);
    button(Button::Start, true);
    EXPECT(!keyboard.ownerActive(owner(Button::Start)));
    button(Button::A, true);
    EXPECT(keyboard.ownerSymbol(owner(Button::A)) == 0x0d);
    button(Button::B, true);
    EXPECT(keyboard.ownerSymbol(owner(Button::B)) == 0x1b);
    button(Button::X, true);
    EXPECT(keyboard.ownerSymbol(owner(Button::X)) == 0x116);
    button(Button::DPadLeft, true);
    EXPECT(keyboard.ownerSymbol(owner(Button::DPadLeft)) == 0x114);
    button(Button::R1, true);
    EXPECT(!keyboard.ownerActive(owner(Button::R1)));
    button(Button::R2, true);
    button(Button::RightStick, true);
    EXPECT(!keyboard.ownerActive(owner(Button::R2)));
    EXPECT(keyboard.ownerSymbol(owner(Button::RightStick)) == ' ');
    axis(Axis::LeftY, -1.0f);
    EXPECT(keyboard.ownerSymbol(GuestOwnerDefaultAxisBase + 2) == 0x111);
    button(Button::Y, true);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 4));
    button(Button::L1, true);
    EXPECT(sink.mapped_keys == ((UINT64_C(1) << 4) | (UINT64_C(1) << 33)));
    EXPECT(!keyboard.ownerActive(owner(Button::L1)));
    // SELECT retains its keyboard toggle and releases both key sources.
    button(Button::Select, true);
    EXPECT(!router.visible());
    button(Button::Select, false);
    EXPECT(router.visible() && sink.mapped_keys == 0);
    EXPECT(!keyboard.ownerActive(owner(Button::A)));
    router.lifecycleCleanup(now++);

    // Overrides replace an individual default, while unrelated controls survive.
    loadMapping(router, "START:SPACE,L1:A,LS_U:W", now++);
    button(Button::Start, true);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 55));
    EXPECT(!keyboard.ownerActive(owner(Button::Start)));
    button(Button::L1, true);
    EXPECT(!keyboard.ownerActive(owner(Button::L1)));
    EXPECT(sink.mapped_keys == ((UINT64_C(1) << 55) | (UINT64_C(1) << 1)));
    axis(Axis::LeftY, -1.0f);
    EXPECT(!keyboard.ownerActive(GuestOwnerDefaultAxisBase + 2));
    EXPECT(sink.mapped_keys & (UINT64_C(1) << 23));
    axis(Axis::LeftY, 1.0f);
    EXPECT(keyboard.ownerSymbol(GuestOwnerDefaultAxisBase + 3) == 0x112);
    button(Button::L2, true);
    EXPECT(!keyboard.ownerActive(owner(Button::L2)));
    router.lifecycleCleanup(now++);

    // Modifier chords suppress defaults only while the complete chord matches.
    loadMapping(router, "SELECT+START:SHIFT+BREAK,SELECT+A:1", now++);
    button(Button::Select, true);
    EXPECT(!router.visible() && !keyboard.ownerActive(owner(Button::Select)));
    button(Button::Start, true);
    EXPECT(!keyboard.ownerActive(owner(Button::Select)));
    EXPECT(!keyboard.ownerActive(owner(Button::Start)));
    EXPECT(sink.mapped_keys == ((UINT64_C(1) << 56) | (UINT64_C(1) << 50)));
    button(Button::DPadUp, true);
    EXPECT(keyboard.ownerSymbol(owner(Button::DPadUp)) == 0x111);
    button(Button::Start, false);
    EXPECT(sink.mapped_keys == 0);
    EXPECT(!router.visible() && !keyboard.ownerActive(owner(Button::Select)));
    button(Button::A, true);
    EXPECT(!router.visible() && sink.mapped_keys == (UINT64_C(1) << 33));
    EXPECT(!keyboard.ownerActive(owner(Button::Select)));
    button(Button::A, false);
    EXPECT(!router.visible() && !keyboard.ownerActive(owner(Button::Select)));
    button(Button::Select, false);
    EXPECT(!router.visible()); // Using SELECT in a chord consumes its tap.
    button(Button::Select, true);
    button(Button::Select, false);
    EXPECT(router.visible());
    router.lifecycleCleanup(now++);

    loadMapping(router, "SELECT:D", now++);
    button(Button::Select, true);
    EXPECT(!router.visible() && sink.mapped_keys == (UINT64_C(1) << 4));
    button(Button::Select, false);
    EXPECT(!router.visible());
    loadMapping(router, "", now++);
    button(Button::Start, true);
    EXPECT(!keyboard.ownerActive(owner(Button::Start)));
    button(Button::A, true);
    EXPECT(keyboard.ownerSymbol(owner(Button::A)) == 0x0d);
    button(Button::Select, true);
    button(Button::Select, false);
    EXPECT(router.visible());

    router.lifecycleCleanup(now++);
    loadMapping(router, "X:F", now++);
    button(Button::X, true);
    EXPECT(sink.mapped_keys == (UINT64_C(1) << 6));
    EXPECT(!keyboard.ownerActive(owner(Button::X)));
    button(Button::X, false);
    loadMapping(router, "", now++);
    button(Button::X, true);
    EXPECT(sink.mapped_keys == 0);
    EXPECT(keyboard.ownerSymbol(owner(Button::X)) == 0x116);
}

#ifndef RG35XX_TEST_NO_BRIDGE
void testBridgeModesAndConsumption()
{
    SDL_setenv("SDLTRS_RG35XX_INPUT", "", 1);
    EXPECT(rg35xx_bridge_init() == 0);
    SDL_Event event = keyEvent(SDL_SCANCODE_F6, true, 1);
    EXPECT(rg35xx_bridge_handle_event(&event, 1) == 0);

    SDL_setenv("SDLTRS_RG35XX_INPUT", "not-a-backend", 1);
    EXPECT(rg35xx_bridge_init() == 0);
    EXPECT(rg35xx_bridge_enabled() == 0);
    EXPECT(rg35xx_bridge_mapping_out('R') == 0);

    SDL_setenv("SDLTRS_RG35XX_INPUT", "sdl", 1);
    SDL_setenv("SDLTRS_RG35XX_MAP", "", 1);
    EXPECT(rg35xx_bridge_init() == 0);
    EXPECT(rg35xx_bridge_enabled() == 0);

    SDL_setenv("SDLTRS_RG35XX_MAP",
               "a=b0,b=b1,up=h0u,down=h0d,left=h0l,right=h0r", 1);
    SDL_setenv("SDLTRS_RG35XX_GUID", "not-a-guid", 1);
    EXPECT(rg35xx_bridge_init() == 0);
    EXPECT(rg35xx_bridge_enabled() == 0);
    SDL_setenv("SDLTRS_RG35XX_GUID", "", 1);
    SDL_setenv("SDLTRS_RG35XX_DEVICE", "-1", 1);
    EXPECT(rg35xx_bridge_init() == 0);
    EXPECT(rg35xx_bridge_enabled() == 0);
    SDL_setenv("SDLTRS_RG35XX_DEVICE", "", 1);

    SDL_setenv("SDLTRS_RG35XX_INPUT", "keyboard", 1);
    EXPECT(rg35xx_bridge_init() == 1);
    EXPECT(rg35xx_bridge_enabled() == 1);
    const char* mapping = "B:SHIFT+BREAK";
    while (*mapping) EXPECT(rg35xx_bridge_mapping_out(*mapping++) == 1);
    EXPECT(rg35xx_bridge_mapping_out(0) == 1);
    SDL_Event mapped_event = keyEvent(SDL_SCANCODE_SPACE, true, 1);
    EXPECT(rg35xx_bridge_handle_event(&mapped_event, 1) == 1);
    EXPECT(bridge_mapped_keys == ((UINT64_C(1) << 56) | (UINT64_C(1) << 50)));
    mapped_event = keyEvent(SDL_SCANCODE_SPACE, false, 2);
    EXPECT(rg35xx_bridge_handle_event(&mapped_event, 2) == 1);
    EXPECT(bridge_mapped_keys == 0);
    EXPECT(rg35xx_bridge_mapping_out(0) == 1);
    event = keyEvent(SDL_SCANCODE_ESCAPE, true, 1);
    EXPECT(rg35xx_bridge_handle_event(&event, 1) == 1);
    event = keyEvent(SDL_SCANCODE_ESCAPE, false, 2);
    EXPECT(rg35xx_bridge_handle_event(&event, 2) == 1);
    EXPECT(rg35xx_bridge_dirty() == 1);

    event = keyEvent(SDL_SCANCODE_Z, true, 3);
    EXPECT(rg35xx_bridge_handle_event(&event, 3) == 0);
    event = {};
    event.type = SDL_QUIT;
    EXPECT(rg35xx_bridge_handle_event(&event, 4) == 0);

    bridge_guest_events.clear();
    const int owner = rg35xx_bridge_physical_owner(SDL_SCANCODE_Z);
    EXPECT(rg35xx_bridge_guest_key_event(owner, SDLK_z, 1) == 1);
    EXPECT(rg35xx_bridge_guest_key_event(owner, SDLK_z, 0) == 1);
    EXPECT(bridge_guest_events.size() == 2);
    EXPECT(bridge_guest_events[0] == SDLK_z);
    EXPECT(bridge_guest_events[1] == (0x10000 | SDLK_z));

    event = keyEvent(SDL_SCANCODE_F5, true, 10);
    EXPECT(rg35xx_bridge_handle_event(&event, 10) == 1);
    EXPECT(rg35xx_bridge_menu_key() == SDLK_F7);
    rg35xx_bridge_modal_begin(11);
    event = keyEvent(SDL_SCANCODE_F5, false, 12);
    EXPECT(rg35xx_bridge_handle_event(&event, 12) == 1);
    EXPECT(rg35xx_bridge_menu_key() == 0);
    event = keyEvent(SDL_SCANCODE_DOWN, true, 13);
    EXPECT(rg35xx_bridge_handle_event(&event, 13) == 1);
    EXPECT(rg35xx_bridge_menu_key() == SDLK_DOWN);
    rg35xx_bridge_update(313);
    EXPECT(rg35xx_bridge_menu_key() == SDLK_DOWN);
    rg35xx_bridge_text_entry(1, 314);
    event = keyEvent(SDL_SCANCODE_SPACE, true, 315);
    EXPECT(rg35xx_bridge_handle_event(&event, 315) == 1);
    EXPECT(rg35xx_bridge_menu_key() == 'q');
    rg35xx_bridge_text_entry(0, 316);
    rg35xx_bridge_modal_end(317);
    EXPECT(rg35xx_bridge_menu_key() == 0);

    SDL_setenv("SDLTRS_RG35XX_INPUT", "off", 1);
    EXPECT(rg35xx_bridge_init() == 0);
    EXPECT(rg35xx_bridge_enabled() == 0);
    event = keyEvent(SDL_SCANCODE_F6, true, 5);
    EXPECT(rg35xx_bridge_handle_event(&event, 5) == 0);
    SDL_setenv("SDLTRS_RG35XX_INPUT", "", 1);
}
#endif

#ifndef RG35XX_TEST_NO_RENDERER
std::vector<Uint32> readRenderer(SDL_Renderer* renderer, int width, int height)
{
    std::vector<Uint32> pixels(static_cast<std::size_t>(width * height));
    EXPECT(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                &pixels[0], width * sizeof(Uint32)) == 0);
    return pixels;
}

void testSoftwareRenderer()
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    EXPECT(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface* output = SDL_CreateRGBSurfaceWithFormat(
        0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface* base = SDL_CreateRGBSurfaceWithFormat(
        0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    EXPECT(output != NULL && base != NULL);
    if (output == NULL || base == NULL) return;
    SDL_FillRect(base, NULL, SDL_MapRGB(base->format, 30, 60, 90));
    const Uint32 saved_base_pixel = *static_cast<Uint32*>(base->pixels);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(output);
    EXPECT(renderer != NULL);
    if (renderer == NULL) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, base);
    EXPECT(texture != NULL);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    const std::vector<Uint32> before = readRenderer(renderer, 640, 480);

    SDL_SetRenderDrawColor(renderer, 3, 4, 5, 6);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    SoftKeyboard keyboard;
    SoftKeyboardRenderer overlay;
    overlay.draw(renderer, keyboard);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    SDL_GetRenderDrawBlendMode(renderer, &blend);
    EXPECT(r == 3 && g == 4 && b == 5 && a == 6);
    EXPECT(blend == SDL_BLENDMODE_ADD);

    const std::vector<Uint32> after = readRenderer(renderer, 640, 480);
    EXPECT(before[20 * 640 + 20] == after[20 * 640 + 20]);
    EXPECT(before[450 * 640 + 320] != after[450 * 640 + 320]);
    EXPECT(*static_cast<Uint32*>(base->pixels) == saved_base_pixel);
    const char* capture = std::getenv("SDLTRS_TEST_CAPTURE");
    if (capture != NULL)
        EXPECT(SDL_SaveBMP(output, capture) == 0);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    const std::vector<Uint32> closed = readRenderer(renderer, 640, 480);
    EXPECT(closed[450 * 640 + 320] == before[450 * 640 + 320]);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(base);
    SDL_FreeSurface(output);

    output = SDL_CreateRGBSurfaceWithFormat(
        0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
    renderer = SDL_CreateSoftwareRenderer(output);
    EXPECT(renderer != NULL);
    if (renderer != NULL) {
        SDL_RenderSetLogicalSize(renderer, 800, 600);
        overlay.draw(renderer, keyboard);
        SDL_DestroyRenderer(renderer);
    }
    SDL_FreeSurface(output);
    SDL_Quit();
}
#endif

} // namespace

int main()
{
    testDefaultBindings();
    testSystemVolumeButtons();
    testSDLJoystickMappingAndTranslation();
    testPhysicalPassthroughPolicy();
    testDefaultGuestBindings();
    testPhysicalHatCaptureAndGating();
    testToggleAndEntryGating();
    testNavigationAndRepeat();
    testGuestKeysShiftAndOwnership();
    testLifecycleCleanup();
    testControlMappingParserAndResolution();
    testControlMappingRoutingAndReceiver();
    testControlMappingPhysicalConsumption();
    testCompactMappings();
    testCustomMappingsAugmentDefaults();
    testMenuControls();
    testMenuTextEntry();
#ifndef RG35XX_TEST_NO_BRIDGE
    testBridgeModesAndConsumption();
#endif
#ifndef RG35XX_TEST_NO_RENDERER
    testSoftwareRenderer();
#endif

    if (failures != 0) {
        std::fprintf(stderr, "%d RG35XX test assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
#ifdef RG35XX_TEST_NO_RENDERER
    std::puts("RG35XX controls, routing, keyboard, and lifecycle tests passed");
#else
    std::puts("RG35XX controls, routing, keyboard, lifecycle, and renderer tests passed");
#endif
    return EXIT_SUCCESS;
}
