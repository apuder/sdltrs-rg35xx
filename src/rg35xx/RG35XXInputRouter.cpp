#include "RG35XXInputRouter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rg35xx {

constexpr float RG35XXInputRouter::kAxisEngageThreshold;
constexpr float RG35XXInputRouter::kAxisReleaseThreshold;

RG35XXInputRouter::RG35XXInputRouter(RG35XXControls& controls,
                                     TrsKeyboardAdapter& keyboard,
                                     bool reserve_all_when_hidden)
    : controls_(controls),
      keyboard_(keyboard),
      text_entry_(false),
      visible_(false),
      dirty_(false),
      suspended_(false),
      reserve_all_when_hidden_(reserve_all_when_hidden),
      analog_x_direction_(0),
      analog_y_direction_(0),
      navigation_direction_(NavigationDirection::None),
      next_repeat_ms_(0),
      b_consumes_shift_(false),
      mapping_active_(false),
      mapping_overflow_(false)
{
    std::fill(mapped_axis_directions_, mapped_axis_directions_ +
              static_cast<std::size_t>(Axis::Count), 0);
    std::fill(gated_buttons_,
              gated_buttons_ + static_cast<std::size_t>(Button::Count), false);
    std::fill(gated_axes_, gated_axes_ + static_cast<std::size_t>(Axis::Count),
              false);
    std::fill(captured_buttons_,
              captured_buttons_ + static_cast<std::size_t>(Button::Count),
              false);
    std::fill(captured_axes_,
              captured_axes_ + static_cast<std::size_t>(Axis::Count), false);
    std::fill(passthrough_buttons_,
              passthrough_buttons_ + static_cast<std::size_t>(Button::Count),
              false);
    std::fill(passthrough_axes_,
              passthrough_axes_ + static_cast<std::size_t>(Axis::Count), false);
}

bool RG35XXInputRouter::handleSDLEvent(const SDL_Event& event,
                                       std::uint32_t now_ms)
{
    if (event.type == SDL_WINDOWEVENT &&
        event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        lifecycleCleanup(now_ms);
        return false;
    }

    const bool recognized = controls_.handleSDLEvent(event);
    if (!recognized)
        return false;
    if (reserve_all_when_hidden_ || suspended_ || mapping_active_) {
        drainControlEvents(now_ms, false);
        return true;
    }
    return drainControlEvents(now_ms, true);
}

void RG35XXInputRouter::update(std::uint32_t now_ms)
{
    controls_.update(now_ms);
    drainControlEvents(now_ms, false);
    if ((!visible_ && !suspended_) || navigation_direction_ == NavigationDirection::None)
        return;

    unsigned int repeats = 0;
    while (deadlineReached(now_ms, next_repeat_ms_) && repeats++ < 32) {
        moveNavigation(navigation_direction_);
        next_repeat_ms_ += kRepeatIntervalMs;
    }
}

bool RG35XXInputRouter::drainControlEvents(std::uint32_t now_ms,
                                           bool route_consumption)
{
    bool consumed = route_consumption && visible_;
    bool passthrough = false;
    ControlEvent event;
    while (controls_.popEvent(event)) {
        bool transition_consumed = false;
        if (!suspended_ && !reserve_all_when_hidden_) {
            transition_consumed = transitionConsumed(event, passthrough);
        }
        if (route_consumption && transition_consumed)
            consumed = true;
        processControlEvent(event, now_ms);
    }
    if (mapping_active_ && !visible_ && !suspended_)
        updateMappedKeys();
    return route_consumption && !passthrough && consumed;
}

bool RG35XXInputRouter::transitionConsumed(const ControlEvent& event,
                                           bool& passthrough)
{
    if (event.type == ControlEventType::AxisChanged) {
        const std::size_t axis = static_cast<std::size_t>(event.axis);
        if (passthrough_axes_[axis]) {
            passthrough = true;
            if (std::fabs(event.value) <= kAxisReleaseThreshold)
                passthrough_axes_[axis] = false;
            return false;
        }
        if (captured_axes_[axis]) {
            if (std::fabs(event.value) <= kAxisReleaseThreshold)
                captured_axes_[axis] = false;
            return true;
        }
        const bool default_axis = event.axis == Axis::LeftX || event.axis == Axis::LeftY;
        if (!visible_ && !default_axis) {
            passthrough = true;
            if (std::fabs(event.value) > kAxisReleaseThreshold)
                passthrough_axes_[axis] = true;
            return false;
        }
        if ((visible_ || default_axis) && std::fabs(event.value) > kAxisReleaseThreshold)
            captured_axes_[axis] = true;
        return visible_ || default_axis;
    }

    const std::size_t button = static_cast<std::size_t>(event.button);
    const bool reserved = event.button == Button::A || event.button == Button::Menu ||
                        defaultButtonSymbol(event.button) >= 0;
    if (passthrough_buttons_[button]) {
        passthrough = true;
        if (event.type == ControlEventType::ButtonUp)
            passthrough_buttons_[button] = false;
        return false;
    }
    if (captured_buttons_[button]) {
        if (event.type == ControlEventType::ButtonUp)
            captured_buttons_[button] = false;
        return true;
    }
    if (!visible_ && !reserved) {
        passthrough = true;
        if (event.type == ControlEventType::ButtonDown)
            passthrough_buttons_[button] = true;
        return false;
    }
    if (event.type == ControlEventType::ButtonDown && (visible_ || reserved))
        captured_buttons_[button] = true;
    return visible_ || reserved;
}

void RG35XXInputRouter::processControlEvent(const ControlEvent& event,
                                            std::uint32_t now_ms)
{
    if (event.type == ControlEventType::AxisChanged) {
        const std::size_t axis = static_cast<std::size_t>(event.axis);
        if (gated_axes_[axis]) {
            if (std::fabs(event.value) <= kAxisReleaseThreshold)
                gated_axes_[axis] = false;
            updateNavigation(now_ms);
            return;
        }
    } else {
        const std::size_t button = static_cast<std::size_t>(event.button);
        if (gated_buttons_[button]) {
            if (event.type == ControlEventType::ButtonUp)
                gated_buttons_[button] = false;
            if (event.button == Button::DPadUp ||
                event.button == Button::DPadDown ||
                event.button == Button::DPadLeft ||
                event.button == Button::DPadRight)
                updateNavigation(now_ms);
            return;
        }
    }

    if (suspended_ && text_entry_) {
        if (event.type == ControlEventType::ButtonDown) {
            int key = 0;
            switch (event.button) {
                case Button::A:
                    if (visible_) closeOverlay(false, now_ms);
                    else openOverlay(now_ms);
                    return;
                case Button::B:
                    if (!visible_) break;
                    if (soft_keyboard_.selectedIsShift()) {
                        soft_keyboard_.toggleShift();
                    } else {
                        key = soft_keyboard_.textSymbol();
                        soft_keyboard_.clearShift();
                    }
                    markDirty();
                    break;
                case Button::X:
                    soft_keyboard_.toggleShift();
                    markDirty();
                    return;
                case Button::Y: key = SDLK_BACKSPACE; break;
                case Button::Start: key = SDLK_RETURN; break;
                case Button::Select:
                case Button::Menu: key = SDLK_ESCAPE; break;
                case Button::L1: key = SDLK_LEFT; break;
                case Button::R1: key = SDLK_RIGHT; break;
                case Button::L2: key = SDLK_F9; break;
                case Button::R2: key = SDLK_TAB; break;
                default: break;
            }
            if (key && menu_keys_.size() < 32) menu_keys_.push_back(key);
        }
        updateNavigation(now_ms);
        return;
    }
    if (suspended_) {
        if (event.type == ControlEventType::ButtonDown) {
            int key = 0;
            switch (event.button) {
                case Button::A:
                case Button::Start: key = SDLK_RETURN; break;
                case Button::B:
                case Button::Select:
                case Button::Menu: key = SDLK_ESCAPE; break;
                case Button::X: key = SDLK_TAB; break;
                case Button::Y: key = SDLK_BACKSPACE; break;
                case Button::L1: key = SDLK_PAGEUP; break;
                case Button::R1: key = SDLK_PAGEDOWN; break;
                default: break;
            }
            if (key && menu_keys_.size() < 32)
                menu_keys_.push_back(key);
        }
        updateNavigation(now_ms);
        return;
    }
    if (event.type == ControlEventType::ButtonDown && event.button == Button::Menu) {
        closeOverlay(false, now_ms);
        menu_keys_.push_back(SDLK_F7);
        return;
    }

    if (!visible_) {
        if (event.type == ControlEventType::ButtonDown &&
            event.button == Button::A &&
            (!mapping_active_ || !(mapping_.overriddenControls(mappedControls()) &
                                   ControlMapping::buttonMask(Button::A))))
            openOverlay(now_ms);
        else if (!mapping_active_)
            updateDefaultKeys();
        return;
    }

    if (event.type == ControlEventType::ButtonDown) {
        switch (event.button) {
            case Button::A:
            case Button::Select:
                closeOverlay(false, now_ms);
                return;
            case Button::B:
                if (soft_keyboard_.selectedIsShift()) {
                    soft_keyboard_.toggleShift();
                    markDirty();
                } else {
                    const bool shifted = soft_keyboard_.shiftLatched();
                    const int symbol = soft_keyboard_.symbol(shifted);
                    const bool pressed =
                        keyboard_.press(GuestOwnerOverlayB, symbol);
                    b_consumes_shift_ = shifted && pressed;
                }
                return;
            case Button::X:
                soft_keyboard_.toggleShift();
                markDirty();
                return;
            case Button::Y:
                keyboard_.press(GuestOwnerOverlayY, 0x114);
                return;
            case Button::Start:
                keyboard_.press(GuestOwnerOverlayStart, 0x0d);
                return;
            default:
                break;
        }
    } else if (event.type == ControlEventType::ButtonUp) {
        switch (event.button) {
            case Button::B:
                if (keyboard_.release(GuestOwnerOverlayB) && b_consumes_shift_) {
                    soft_keyboard_.clearShift();
                    markDirty();
                }
                b_consumes_shift_ = false;
                return;
            case Button::Y:
                keyboard_.release(GuestOwnerOverlayY);
                return;
            case Button::Start:
                keyboard_.release(GuestOwnerOverlayStart);
                return;
            default:
                break;
        }
    }

    if (event.type == ControlEventType::AxisChanged ||
        event.button == Button::DPadUp || event.button == Button::DPadDown ||
        event.button == Button::DPadLeft || event.button == Button::DPadRight)
        updateNavigation(now_ms);
}

int RG35XXInputRouter::defaultButtonSymbol(Button button)
{
    switch (button) {
        case Button::DPadUp: return 0x111;
        case Button::DPadDown: return 0x112;
        case Button::DPadLeft: return 0x114;
        case Button::DPadRight: return 0x113;
        case Button::RightStick: return ' ';
        case Button::Start: return 0x0d;
        case Button::Select: return 0x116;
        default: return -1;
    }
}

void RG35XXInputRouter::updateDefaultKeys(std::uint32_t overridden)
{
    const ControlState& state = controls_.state();
    for (std::size_t i = 0; i < static_cast<std::size_t>(Button::Count); ++i) {
        const int symbol = defaultButtonSymbol(static_cast<Button>(i));
        if (symbol < 0) continue;
        const int owner = GuestOwnerDefaultButtonBase + static_cast<int>(i);
        if (state.buttons[i] && !gated_buttons_[i] &&
            !(overridden & ControlMapping::buttonMask(static_cast<Button>(i)))) {
            if (!keyboard_.ownerActive(owner)) keyboard_.press(owner, symbol);
        } else {
            keyboard_.release(owner);
        }
    }

    const Axis axes[] = {Axis::LeftX, Axis::LeftY};
    int* directions[] = {&analog_x_direction_, &analog_y_direction_};
    const int symbols[2][2] = {{0x114, 0x113}, {0x111, 0x112}};
    for (int i = 0; i < 2; ++i) {
        const std::size_t axis = static_cast<std::size_t>(axes[i]);
        const float value = gated_axes_[axis] ? 0.0f : state.axes[axis];
        *directions[i] = hysteresisDirection(value, *directions[i]);
        // Each source owns its key independently, including diagonals and
        // the same arrow held by both the D-pad and the analog stick.
        for (int side = 0; side < 2; ++side) {
            const int owner = GuestOwnerDefaultAxisBase + i * 2 + side;
            const int direction = side == 0 ? -1 : 1;
            if (*directions[i] == direction &&
                !(overridden & ControlMapping::axisMask(axes[i], direction))) {
                if (!keyboard_.ownerActive(owner)) keyboard_.press(owner, symbols[i][side]);
            } else {
                keyboard_.release(owner);
            }
        }
    }
}

void RG35XXInputRouter::gateHeldControls()
{
    std::fill(mapped_axis_directions_, mapped_axis_directions_ +
              static_cast<std::size_t>(Axis::Count), 0);
    const ControlState& state = controls_.state();
    for (std::size_t i = 0; i < static_cast<std::size_t>(Button::Count); ++i)
        gated_buttons_[i] = state.buttons[i];
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i)
        gated_axes_[i] = std::fabs(state.axes[i]) > kAxisReleaseThreshold;
}

void RG35XXInputRouter::openOverlay(std::uint32_t)
{
    keyboard_.releaseSynthetic();
    soft_keyboard_.clearShift();
    b_consumes_shift_ = false;
    analog_x_direction_ = 0;
    analog_y_direction_ = 0;
    navigation_direction_ = NavigationDirection::None;
    gateHeldControls();
    visible_ = true;
    markDirty();
}

void RG35XXInputRouter::closeOverlay(bool release_all, std::uint32_t now_ms)
{
    if (release_all)
        keyboard_.releaseAll();
    else
        keyboard_.releaseSynthetic();
    soft_keyboard_.clearShift();
    b_consumes_shift_ = false;
    analog_x_direction_ = 0;
    analog_y_direction_ = 0;
    navigation_direction_ = NavigationDirection::None;
    next_repeat_ms_ = now_ms;
    gateHeldControls();
    if (visible_) {
        visible_ = false;
        markDirty();
    }
}

int RG35XXInputRouter::hysteresisDirection(float value, int previous)
{
    const float magnitude = std::fabs(value);
    const int sign = value < 0.0f ? -1 : (value > 0.0f ? 1 : 0);
    if (previous == 0)
        return magnitude >= kAxisEngageThreshold ? sign : 0;
    if (magnitude <= kAxisReleaseThreshold)
        return 0;
    if (sign != previous && magnitude >= kAxisEngageThreshold)
        return sign;
    return previous;
}

NavigationDirection RG35XXInputRouter::desiredDirection()
{
    const ControlState& state = controls_.state();
    int x = (state.pressed(Button::DPadRight) &&
             !gated_buttons_[static_cast<std::size_t>(Button::DPadRight)] ? 1 : 0) -
            (state.pressed(Button::DPadLeft) &&
             !gated_buttons_[static_cast<std::size_t>(Button::DPadLeft)] ? 1 : 0);
    int y = (state.pressed(Button::DPadDown) &&
             !gated_buttons_[static_cast<std::size_t>(Button::DPadDown)] ? 1 : 0) -
            (state.pressed(Button::DPadUp) &&
             !gated_buttons_[static_cast<std::size_t>(Button::DPadUp)] ? 1 : 0);
    float magnitude_x = static_cast<float>(std::abs(x));
    float magnitude_y = static_cast<float>(std::abs(y));

    if (x == 0 && y == 0) {
        const float axis_x = gated_axes_[static_cast<std::size_t>(Axis::LeftX)]
            ? 0.0f : state.value(Axis::LeftX);
        const float axis_y = gated_axes_[static_cast<std::size_t>(Axis::LeftY)]
            ? 0.0f : state.value(Axis::LeftY);
        analog_x_direction_ = hysteresisDirection(axis_x,
                                                   analog_x_direction_);
        analog_y_direction_ = hysteresisDirection(axis_y,
                                                   analog_y_direction_);
        x = analog_x_direction_;
        y = analog_y_direction_;
        magnitude_x = std::fabs(axis_x);
        magnitude_y = std::fabs(axis_y);
    }

    if (x == 0 && y == 0)
        return NavigationDirection::None;
    if (x != 0 && y != 0 && magnitude_x == magnitude_y) {
        if ((x < 0 && navigation_direction_ == NavigationDirection::Left) ||
            (x > 0 && navigation_direction_ == NavigationDirection::Right))
            y = 0;
        else if ((y < 0 && navigation_direction_ == NavigationDirection::Up) ||
                 (y > 0 && navigation_direction_ == NavigationDirection::Down))
            x = 0;
        else
            y = 0;
    } else if (x != 0 && y != 0) {
        if (magnitude_x > magnitude_y)
            y = 0;
        else
            x = 0;
    }

    if (x < 0) return NavigationDirection::Left;
    if (x > 0) return NavigationDirection::Right;
    if (y < 0) return NavigationDirection::Up;
    return NavigationDirection::Down;
}

void RG35XXInputRouter::updateNavigation(std::uint32_t now_ms)
{
    if (!visible_ && !suspended_)
        return;
    const NavigationDirection desired = desiredDirection();
    if (desired == navigation_direction_)
        return;
    navigation_direction_ = desired;
    if (desired != NavigationDirection::None) {
        moveNavigation(desired);
        next_repeat_ms_ = now_ms + kRepeatDelayMs;
    }
}

void RG35XXInputRouter::moveNavigation(NavigationDirection direction)
{
    if (suspended_ && !(text_entry_ && visible_)) {
        int key = 0;
        switch (direction) {
            case NavigationDirection::Up: key = SDLK_UP; break;
            case NavigationDirection::Down: key = SDLK_DOWN; break;
            case NavigationDirection::Left: key = SDLK_LEFT; break;
            case NavigationDirection::Right: key = SDLK_RIGHT; break;
            default: break;
        }
        if (key && menu_keys_.size() < 32)
            menu_keys_.push_back(key);
    } else if (soft_keyboard_.move(direction)) {
        markDirty();
    }
}

bool RG35XXInputRouter::deadlineReached(std::uint32_t now,
                                        std::uint32_t deadline)
{
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

void RG35XXInputRouter::setSuspended(bool suspended, std::uint32_t now_ms)
{
    if (suspended == suspended_)
        return;
    if (suspended) {
        closeOverlay(true, now_ms);
        controls_.releaseAll(now_ms);
        ControlEvent discarded;
        while (controls_.popEvent(discarded)) {}
        std::fill(gated_buttons_,
                  gated_buttons_ + static_cast<std::size_t>(Button::Count), false);
        std::fill(gated_axes_,
                  gated_axes_ + static_cast<std::size_t>(Axis::Count), false);
        if (reserve_all_when_hidden_) {
            std::fill(captured_buttons_,
                      captured_buttons_ +
                          static_cast<std::size_t>(Button::Count), false);
            std::fill(captured_axes_, captured_axes_ +
                      static_cast<std::size_t>(Axis::Count), false);
        }
    }
    text_entry_ = false;
    closeOverlay(false, now_ms);
    menu_keys_.clear();
    navigation_direction_ = NavigationDirection::None;
    analog_x_direction_ = analog_y_direction_ = 0;
    gateHeldControls();
    suspended_ = suspended;
}

void RG35XXInputRouter::lifecycleCleanup(std::uint32_t now_ms)
{
    mapping_buffer_.clear();
    mapping_overflow_ = false;
    menu_keys_.clear();
    closeOverlay(true, now_ms);
    controls_.releaseAll(now_ms);
    ControlEvent discarded;
    while (controls_.popEvent(discarded)) {}
    std::fill(gated_buttons_,
              gated_buttons_ + static_cast<std::size_t>(Button::Count), false);
    std::fill(gated_axes_, gated_axes_ + static_cast<std::size_t>(Axis::Count),
              false);
    if (reserve_all_when_hidden_) {
        std::fill(captured_buttons_,
                  captured_buttons_ + static_cast<std::size_t>(Button::Count),
                  false);
        std::fill(captured_axes_,
                  captured_axes_ + static_cast<std::size_t>(Axis::Count), false);
    }
}

bool RG35XXInputRouter::mappingByte(unsigned char value, std::uint32_t now_ms)
{
    if (value != 0) {
        if (mapping_buffer_.size() == ControlMapping::kMaxLength)
            mapping_overflow_ = true;
        if (!mapping_overflow_) mapping_buffer_ += static_cast<char>(value);
        return true;
    }

    std::string text;
    text.swap(mapping_buffer_);
    const bool overflow = mapping_overflow_;
    mapping_overflow_ = false;
    ControlMapping parsed;
    std::string error;
    if (overflow || (!text.empty() && !ControlMapping::parse(text, parsed, error))) {
        std::fprintf(stderr, "SDLTRS RG35XX: rejected port 41 mapping: %s\n",
                     overflow ? "more than 4095 bytes" : error.c_str());
        return false;
    }
    closeOverlay(false, now_ms);
    mapping_ = parsed;
    mapping_active_ = !text.empty();
    return true;
}

void RG35XXInputRouter::updateMappedKeys()
{
    const std::uint32_t controls = mappedControls();
    updateDefaultKeys(mapping_.overriddenControls(controls));
    keyboard_.setMappedKeys(mapping_.resolve(controls));
}

std::uint32_t RG35XXInputRouter::mappedControls()
{
    const ControlState& state = controls_.state();
    std::uint32_t controls = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(Button::Count); ++i)
        if (state.buttons[i] && !gated_buttons_[i])
            controls |= ControlMapping::buttonMask(static_cast<Button>(i));
    for (std::size_t i = 0; i < static_cast<std::size_t>(Axis::Count); ++i) {
        mapped_axis_directions_[i] = hysteresisDirection(
            gated_axes_[i] ? 0.0f : state.axes[i], mapped_axis_directions_[i]);
        controls |= ControlMapping::axisMask(static_cast<Axis>(i), mapped_axis_directions_[i]);
    }
    return controls;
}

bool RG35XXInputRouter::guestKeyEvent(int owner, int symbol, bool down)
{
    return down ? keyboard_.press(owner, symbol) : keyboard_.release(owner);
}

void RG35XXInputRouter::setTextEntry(bool enabled, std::uint32_t now_ms)
{
    if (!suspended_) return;
    menu_keys_.clear();
    text_entry_ = enabled;
    if (enabled) openOverlay(now_ms);
    else closeOverlay(false, now_ms);
    gateHeldControls();
}

bool RG35XXInputRouter::textEntry() const { return text_entry_; }

int RG35XXInputRouter::popMenuKey()
{
    if (menu_keys_.empty()) return 0;
    const int key = menu_keys_.front();
    menu_keys_.pop_front();
    return key;
}

bool RG35XXInputRouter::visible() const
{
    return visible_;
}

bool RG35XXInputRouter::dirty() const
{
    return dirty_;
}

void RG35XXInputRouter::clearDirty()
{
    dirty_ = false;
}

const SoftKeyboard& RG35XXInputRouter::softKeyboard() const
{
    return soft_keyboard_;
}

SoftKeyboard& RG35XXInputRouter::softKeyboardForTesting()
{
    return soft_keyboard_;
}

NavigationDirection RG35XXInputRouter::navigationDirection() const
{
    return navigation_direction_;
}

void RG35XXInputRouter::markDirty()
{
    dirty_ = true;
}

} // namespace rg35xx
