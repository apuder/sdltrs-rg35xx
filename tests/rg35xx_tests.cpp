#include "../src/rg35xx/KeyboardRG35XXControls.hpp"
#include "../src/rg35xx/RG35XXInputRouter.hpp"
#include "../src/rg35xx/SDLRG35XXControls.hpp"
#include "../src/rg35xx/rg35xx_bridge.h"
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
    void keyDown(int symbol) override { events.push_back({true, symbol}); }
    void keyUp(int symbol) override { events.push_back({false, symbol}); }
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
    tap(router, SDL_SCANCODE_F6, now);
    EXPECT(router.visible());
}

void openOverlay(FakeControls& controls, RG35XXInputRouter& router,
                 std::uint32_t now)
{
    controls.button(Button::A, true, now);
    router.update(now);
    controls.button(Button::A, false, now + 1);
    router.update(now + 1);
    EXPECT(router.visible());
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

    SDL_Event event = keyEvent(SDL_SCANCODE_SPACE, true, 1);
    EXPECT(!router.handleSDLEvent(event, 1));
    event = keyEvent(SDL_SCANCODE_F6, true, 2);
    EXPECT(router.handleSDLEvent(event, 2));
    EXPECT(router.visible());
    event = keyEvent(SDL_SCANCODE_F6, false, 3);
    EXPECT(router.handleSDLEvent(event, 3));
    event = keyEvent(SDL_SCANCODE_SPACE, false, 4);
    EXPECT(!router.handleSDLEvent(event, 4));
    EXPECT(sink.events.empty());

    event = keyEvent(SDL_SCANCODE_SPACE, true, 5);
    EXPECT(router.handleSDLEvent(event, 5));
    event = keyEvent(SDL_SCANCODE_F6, true, 6);
    EXPECT(router.handleSDLEvent(event, 6));
    EXPECT(!router.visible());
    event = keyEvent(SDL_SCANCODE_F6, false, 7);
    EXPECT(router.handleSDLEvent(event, 7));
    event = keyEvent(SDL_SCANCODE_SPACE, false, 8);
    EXPECT(router.handleSDLEvent(event, 8));

    event = keyEvent(SDL_SCANCODE_SPACE, true, 9);
    EXPECT(!router.handleSDLEvent(event, 9));
    event = keyEvent(SDL_SCANCODE_SPACE, false, 10);
    EXPECT(!router.handleSDLEvent(event, 10));

    event = keyEvent(SDL_SCANCODE_A, true, 11);
    EXPECT(!router.handleSDLEvent(event, 11));
    event = keyEvent(SDL_SCANCODE_F6, true, 12);
    EXPECT(router.handleSDLEvent(event, 12));
    event = keyEvent(SDL_SCANCODE_F6, false, 13);
    EXPECT(router.handleSDLEvent(event, 13));
    event = keyEvent(SDL_SCANCODE_A, false, 14);
    EXPECT(!router.handleSDLEvent(event, 14));
}

void testPhysicalHatPassthroughReconciliation()
{
    JoystickTranslatorControls controls(testJoystickMapping(), 42);
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard, false);

    SDL_Event event = joyHatEvent(42, 0, SDL_HAT_UP, 1);
    EXPECT(!router.handleSDLEvent(event, 1));
    event = joyButtonEvent(42, 0, true, 2);
    EXPECT(router.handleSDLEvent(event, 2));
    EXPECT(router.visible());
    event = joyButtonEvent(42, 0, false, 3);
    EXPECT(router.handleSDLEvent(event, 3));
    const int initial = router.softKeyboard().selection();

    event = joyHatEvent(42, 0, SDL_HAT_RIGHTUP, 4);
    EXPECT(!router.handleSDLEvent(event, 4));
    EXPECT(router.softKeyboard().selection() == initial);
    event = joyHatEvent(42, 0, SDL_HAT_RIGHT, 5);
    EXPECT(!router.handleSDLEvent(event, 5));
    EXPECT(router.softKeyboard().selection() == initial);
    event = joyHatEvent(42, 0, SDL_HAT_CENTERED, 6);
    EXPECT(!router.handleSDLEvent(event, 6));
    EXPECT(router.softKeyboard().selection() == initial);

    event = joyHatEvent(42, 0, SDL_HAT_RIGHT, 7);
    EXPECT(router.handleSDLEvent(event, 7));
    EXPECT(router.softKeyboard().selection() == initial + 1);
    event = joyHatEvent(42, 0, SDL_HAT_CENTERED, 8);
    EXPECT(router.handleSDLEvent(event, 8));
}

void testToggleAndEntryGating()
{
    KeyboardRG35XXControls controls;
    FakeSink sink;
    TrsKeyboardAdapter keyboard(sink);
    RG35XXInputRouter router(controls, keyboard);
    std::uint32_t now = 100;

    send(router, SDL_SCANCODE_SPACE, true, now++);
    send(router, SDL_SCANCODE_F6, true, now++);
    EXPECT(router.visible());
    send(router, SDL_SCANCODE_F6, true, now++, true);
    EXPECT(router.visible());
    send(router, SDL_SCANCODE_F6, false, now++);
    send(router, SDL_SCANCODE_SPACE, false, now++);
    EXPECT(sink.events.empty());

    send(router, SDL_SCANCODE_F6, true, now++);
    EXPECT(!router.visible());
    send(router, SDL_SCANCODE_F6, false, now++);
    EXPECT(sink.events.empty());

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
    send(router, SDL_SCANCODE_RETURN, true, now++);
    send(router, SDL_SCANCODE_RETURN, false, now++);
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
    send(router, SDL_SCANCODE_RETURN, true, now++);
    router.setSuspended(true, now++);
    EXPECT(!router.visible());
    EXPECT(sink.events.back().symbol == 0x0d && !sink.events.back().down);
    router.setSuspended(false, now++);

    SDL_Event focus = {};
    focus.type = SDL_WINDOWEVENT;
    focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    EXPECT(!router.handleSDLEvent(focus, now++));
    EXPECT(!router.visible());
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
    EXPECT(rg35xx_bridge_handle_event(&event, 1) == 1);
    event = keyEvent(SDL_SCANCODE_F6, false, 2);
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
    testSDLJoystickMappingAndTranslation();
    testPhysicalPassthroughPolicy();
    testPhysicalHatPassthroughReconciliation();
    testToggleAndEntryGating();
    testNavigationAndRepeat();
    testGuestKeysShiftAndOwnership();
    testLifecycleCleanup();
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
