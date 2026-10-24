#include "KeyboardRG35XXControls.hpp"

#include <algorithm>
#include <cmath>

namespace rg35xx {
namespace {

const HostBinding kBindings[] = {
    {SDL_SCANCODE_F6, HostBindingKind::Button, Button::A, Axis::LeftX},
    {SDL_SCANCODE_SPACE, HostBindingKind::Button, Button::B, Axis::LeftX},
    {SDL_SCANCODE_TAB, HostBindingKind::Button, Button::X, Axis::LeftX},
    {SDL_SCANCODE_BACKSPACE, HostBindingKind::Button, Button::Y, Axis::LeftX},
    {SDL_SCANCODE_UP, HostBindingKind::Button, Button::DPadUp, Axis::LeftX},
    {SDL_SCANCODE_DOWN, HostBindingKind::Button, Button::DPadDown, Axis::LeftX},
    {SDL_SCANCODE_LEFT, HostBindingKind::Button, Button::DPadLeft, Axis::LeftX},
    {SDL_SCANCODE_RIGHT, HostBindingKind::Button, Button::DPadRight, Axis::LeftX},
    {SDL_SCANCODE_W, HostBindingKind::AxisNegative, Button::A, Axis::LeftY},
    {SDL_SCANCODE_S, HostBindingKind::AxisPositive, Button::A, Axis::LeftY},
    {SDL_SCANCODE_A, HostBindingKind::AxisNegative, Button::A, Axis::LeftX},
    {SDL_SCANCODE_D, HostBindingKind::AxisPositive, Button::A, Axis::LeftX},
    {SDL_SCANCODE_I, HostBindingKind::AxisNegative, Button::A, Axis::RightY},
    {SDL_SCANCODE_K, HostBindingKind::AxisPositive, Button::A, Axis::RightY},
    {SDL_SCANCODE_J, HostBindingKind::AxisNegative, Button::A, Axis::RightX},
    {SDL_SCANCODE_L, HostBindingKind::AxisPositive, Button::A, Axis::RightX},
    {SDL_SCANCODE_Q, HostBindingKind::Button, Button::L1, Axis::LeftX},
    {SDL_SCANCODE_E, HostBindingKind::Button, Button::R1, Axis::LeftX},
    {SDL_SCANCODE_1, HostBindingKind::Button, Button::L2, Axis::LeftX},
    {SDL_SCANCODE_3, HostBindingKind::Button, Button::R2, Axis::LeftX},
    {SDL_SCANCODE_C, HostBindingKind::Button, Button::LeftStick, Axis::LeftX},
    {SDL_SCANCODE_V, HostBindingKind::Button, Button::RightStick, Axis::LeftX},
    {SDL_SCANCODE_RETURN, HostBindingKind::Button, Button::Start, Axis::LeftX},
    {SDL_SCANCODE_ESCAPE, HostBindingKind::Button, Button::Select, Axis::LeftX},
    {SDL_SCANCODE_F5, HostBindingKind::Button, Button::Menu, Axis::LeftX}
};

const HostBinding* findBinding(SDL_Scancode scancode)
{
    for (std::size_t i = 0; i < sizeof(kBindings) / sizeof(kBindings[0]); ++i) {
        if (kBindings[i].scancode == scancode)
            return &kBindings[i];
    }
    return NULL;
}

} // namespace

KeyboardRG35XXControls::KeyboardRG35XXControls()
{
    std::fill(pressed_scancodes_, pressed_scancodes_ + SDL_NUM_SCANCODES, false);
}

const HostBinding* KeyboardRG35XXControls::defaultBindings(std::size_t& count)
{
    count = sizeof(kBindings) / sizeof(kBindings[0]);
    return kBindings;
}

bool KeyboardRG35XXControls::pushEvent(const ControlEvent& event)
{
    if (events_.size() >= kQueueCapacity) {
        if (event.type == ControlEventType::ButtonUp ||
            (event.type == ControlEventType::AxisChanged && event.value == 0.0f)) {
            std::deque<ControlEvent>::iterator discard = events_.begin();
            while (discard != events_.end() &&
                   (discard->type == ControlEventType::ButtonUp ||
                    (discard->type == ControlEventType::AxisChanged &&
                     discard->value == 0.0f))) {
                ++discard;
            }
            if (discard != events_.end())
                events_.erase(discard);
            else
                return false;
        } else {
            return false;
        }
    }
    events_.push_back(event);
    return true;
}

float KeyboardRG35XXControls::axisValue(Axis axis) const
{
    bool negative = false;
    bool positive = false;
    for (std::size_t i = 0; i < sizeof(kBindings) / sizeof(kBindings[0]); ++i) {
        if (kBindings[i].axis != axis)
            continue;
        if (kBindings[i].kind == HostBindingKind::AxisNegative)
            negative = negative || pressed_scancodes_[kBindings[i].scancode];
        else if (kBindings[i].kind == HostBindingKind::AxisPositive)
            positive = positive || pressed_scancodes_[kBindings[i].scancode];
    }
    return negative == positive ? 0.0f : (negative ? -1.0f : 1.0f);
}

void KeyboardRG35XXControls::refreshAxis(Axis axis, std::uint32_t timestamp)
{
    const std::size_t index = static_cast<std::size_t>(axis);
    const float value = axisValue(axis);
    if (std::fabs(state_.axes[index] - value) < 0.0001f)
        return;
    const float old_value = state_.axes[index];
    state_.axes[index] = value;
    if (!pushEvent(ControlEvent::axisEvent(axis, value, timestamp)))
        state_.axes[index] = old_value;
}

bool KeyboardRG35XXControls::handleSDLEvent(const SDL_Event& event)
{
    // SDL emits text separately from keydown; reserved simulator controls
    // must not also insert characters into modal text fields.
    if (event.type == SDL_TEXTINPUT && event.text.text[1] == '\0') {
        int character = static_cast<unsigned char>(event.text.text[0]);
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        for (std::size_t i = 0; i < sizeof(kBindings) / sizeof(kBindings[0]); ++i)
            if (SDL_GetKeyFromScancode(kBindings[i].scancode) == character)
                return true;
        return false;
    }
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
        return false;

    const SDL_Scancode scancode = event.key.keysym.scancode;
    const HostBinding* binding = findBinding(scancode);
    if (binding == NULL)
        return false;

    if (event.type == SDL_KEYDOWN && event.key.repeat)
        return true;

    const bool down = event.type == SDL_KEYDOWN;
    if (pressed_scancodes_[scancode] == down)
        return true;
    pressed_scancodes_[scancode] = down;

    if (binding->kind == HostBindingKind::Button) {
        const std::size_t index = static_cast<std::size_t>(binding->button);
        const bool old_state = state_.buttons[index];
        state_.buttons[index] = down;
        if (!pushEvent(ControlEvent::buttonEvent(
                down ? ControlEventType::ButtonDown : ControlEventType::ButtonUp,
                binding->button, event.key.timestamp))) {
            state_.buttons[index] = old_state;
            pressed_scancodes_[scancode] = !down;
        }
    } else {
        refreshAxis(binding->axis, event.key.timestamp);
    }
    return true;
}

void KeyboardRG35XXControls::update(std::uint32_t)
{
}

bool KeyboardRG35XXControls::popEvent(ControlEvent& event)
{
    if (events_.empty())
        return false;
    event = events_.front();
    events_.pop_front();
    return true;
}

const ControlState& KeyboardRG35XXControls::state() const
{
    return state_;
}

void KeyboardRG35XXControls::releaseAll(std::uint32_t now_ms)
{
    std::fill(pressed_scancodes_, pressed_scancodes_ + SDL_NUM_SCANCODES, false);
    for (std::size_t i = 0; i < static_cast<std::size_t>(Button::Count); ++i) {
        if (state_.buttons[i]) {
            state_.buttons[i] = false;
            pushEvent(ControlEvent::buttonEvent(ControlEventType::ButtonUp,
                                                static_cast<Button>(i), now_ms));
        }
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i) {
        if (state_.axes[i] != 0.0f) {
            state_.axes[i] = 0.0f;
            pushEvent(ControlEvent::axisEvent(static_cast<Axis>(i), 0.0f, now_ms));
        }
    }
}

} // namespace rg35xx
