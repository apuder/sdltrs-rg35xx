#ifndef RG35XX_CONTROL_MAPPING_HPP
#define RG35XX_CONTROL_MAPPING_HPP

#include "RG35XXControls.hpp"

#include <string>
#include <vector>

namespace rg35xx {

class ControlMapping {
public:
    // Receives compact CONTROL:KEY pairs. Empty frames clear the map in the router.
    static const std::size_t kMaxLength = 4095;
    static bool parse(const std::string& text, ControlMapping& result,
                      std::string& error);
    static std::uint32_t buttonMask(Button button);
    static std::uint32_t axisMask(Axis axis, int direction);

    std::uint64_t resolve(std::uint32_t controls) const;
    // Defaults for controls in a matching custom chord must be suppressed.
    std::uint32_t overriddenControls(std::uint32_t controls) const;
    // True when any binding references at least one bit from control_mask.
    bool hasBindingWithControl(std::uint32_t control_mask) const;

private:
    struct Binding {
        std::uint32_t controls;
        // Matrix bits are row * 8 + column, independent of host translation.
        std::uint64_t keys;
    };
    std::vector<Binding> bindings_;
};

} // namespace rg35xx

#endif
