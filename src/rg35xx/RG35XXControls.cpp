#include "RG35XXControls.hpp"

#include <algorithm>

namespace rg35xx {

ControlEvent ControlEvent::buttonEvent(ControlEventType event_type,
                                       Button event_button,
                                       std::uint32_t event_timestamp)
{
    ControlEvent event = {};
    event.type = event_type;
    event.timestamp = event_timestamp;
    event.button = event_button;
    event.axis = Axis::LeftX;
    event.value = event_type == ControlEventType::ButtonDown ? 1.0f : 0.0f;
    return event;
}

ControlEvent ControlEvent::axisEvent(Axis event_axis, float event_value,
                                     std::uint32_t event_timestamp)
{
    ControlEvent event = {};
    event.type = ControlEventType::AxisChanged;
    event.timestamp = event_timestamp;
    event.button = Button::A;
    event.axis = event_axis;
    event.value = event_value;
    return event;
}

ControlState::ControlState()
{
    std::fill(buttons, buttons + static_cast<std::size_t>(Button::Count), false);
    std::fill(axes, axes + static_cast<std::size_t>(Axis::Count), 0.0f);
}

bool ControlState::pressed(Button button) const
{
    return buttons[static_cast<std::size_t>(button)];
}

float ControlState::value(Axis axis) const
{
    return axes[static_cast<std::size_t>(axis)];
}

} // namespace rg35xx
