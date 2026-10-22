#include "RG35XXInputRouter.hpp"

#include <algorithm>
#include <cmath>

namespace rg35xx {

constexpr float RG35XXInputRouter::kAxisEngageThreshold;
constexpr float RG35XXInputRouter::kAxisReleaseThreshold;

RG35XXInputRouter::RG35XXInputRouter(RG35XXControls& controls,
                                     TrsKeyboardAdapter& keyboard)
    : controls_(controls),
      keyboard_(keyboard),
      visible_(false),
      dirty_(false),
      suspended_(false),
      analog_x_direction_(0),
      analog_y_direction_(0),
      navigation_direction_(NavigationDirection::None),
      next_repeat_ms_(0),
      b_consumes_shift_(false)
{
    std::fill(gated_buttons_,
              gated_buttons_ + static_cast<std::size_t>(Button::Count), false);
    std::fill(gated_axes_, gated_axes_ + static_cast<std::size_t>(Axis::Count),
              false);
}

bool RG35XXInputRouter::handleSDLEvent(const SDL_Event& event,
                                       std::uint32_t now_ms)
{
    if (event.type == SDL_WINDOWEVENT &&
        event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        lifecycleCleanup(now_ms);
        return false;
    }

    if (suspended_)
        return false;

    const bool recognized = controls_.handleSDLEvent(event);
    if (recognized)
        drainControlEvents(now_ms);
    return recognized;
}

void RG35XXInputRouter::update(std::uint32_t now_ms)
{
    if (suspended_)
        return;

    controls_.update(now_ms);
    drainControlEvents(now_ms);
    if (!visible_ || navigation_direction_ == NavigationDirection::None)
        return;

    unsigned int repeats = 0;
    while (deadlineReached(now_ms, next_repeat_ms_) && repeats++ < 32) {
        if (soft_keyboard_.move(navigation_direction_))
            markDirty();
        next_repeat_ms_ += kRepeatIntervalMs;
    }
}

void RG35XXInputRouter::drainControlEvents(std::uint32_t now_ms)
{
    ControlEvent event;
    while (controls_.popEvent(event))
        processControlEvent(event, now_ms);
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

    if (!visible_) {
        if (event.type == ControlEventType::ButtonDown &&
            event.button == Button::A)
            openOverlay(now_ms);
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

void RG35XXInputRouter::gateHeldControls()
{
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
    if (!visible_)
        return;
    const NavigationDirection desired = desiredDirection();
    if (desired == navigation_direction_)
        return;
    navigation_direction_ = desired;
    if (desired != NavigationDirection::None) {
        if (soft_keyboard_.move(desired))
            markDirty();
        next_repeat_ms_ = now_ms + kRepeatDelayMs;
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
    }
    suspended_ = suspended;
}

void RG35XXInputRouter::lifecycleCleanup(std::uint32_t now_ms)
{
    closeOverlay(true, now_ms);
    controls_.releaseAll(now_ms);
    ControlEvent discarded;
    while (controls_.popEvent(discarded)) {}
    std::fill(gated_buttons_,
              gated_buttons_ + static_cast<std::size_t>(Button::Count), false);
    std::fill(gated_axes_, gated_axes_ + static_cast<std::size_t>(Axis::Count),
              false);
}

bool RG35XXInputRouter::guestKeyEvent(int owner, int symbol, bool down)
{
    return down ? keyboard_.press(owner, symbol) : keyboard_.release(owner);
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
