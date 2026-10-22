#ifndef KEYBOARD_RG35XX_CONTROLS_HPP
#define KEYBOARD_RG35XX_CONTROLS_HPP

#include "RG35XXControls.hpp"

#include <deque>

namespace rg35xx {

enum class HostBindingKind : std::uint8_t {
    Button,
    AxisNegative,
    AxisPositive
};

struct HostBinding {
    SDL_Scancode scancode;
    HostBindingKind kind;
    Button button;
    Axis axis;
};

class KeyboardRG35XXControls : public RG35XXControls {
public:
    KeyboardRG35XXControls();

    bool handleSDLEvent(const SDL_Event& event) override;
    void update(std::uint32_t now_ms) override;
    bool popEvent(ControlEvent& event) override;
    const ControlState& state() const override;
    void releaseAll(std::uint32_t now_ms) override;

    static const HostBinding* defaultBindings(std::size_t& count);

private:
    static const std::size_t kQueueCapacity = 128;

    bool pushEvent(const ControlEvent& event);
    float axisValue(Axis axis) const;
    void refreshAxis(Axis axis, std::uint32_t timestamp);

    ControlState state_;
    bool pressed_scancodes_[SDL_NUM_SCANCODES];
    std::deque<ControlEvent> events_;
};

} // namespace rg35xx

#endif
