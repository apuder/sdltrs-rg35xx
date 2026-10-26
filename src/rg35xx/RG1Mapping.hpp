#ifndef RG35XX_RG1_MAPPING_HPP
#define RG35XX_RG1_MAPPING_HPP

#include "RG35XXControls.hpp"

#include <string>
#include <vector>

namespace rg35xx {

// Matrix bits are row * 8 + column, independent of host key translation.
struct RG1Action {
    enum Kind { Keys, AnyKey, TextInput, NumericInput } kind;
    std::uint64_t keys;
};

struct RG1Binding {
    enum Kind { Physical, Prompt, Overlay } kind;
    std::uint32_t controls;
    std::vector<RG1Action> actions;
};

class RG1Mapping {
public:
    static const std::size_t kMaxLength = 4095;
    static bool parse(const std::string& text, RG1Mapping& result,
                      std::string& error);
    static std::uint32_t buttonMask(Button button);
    static std::uint32_t axisMask(Axis axis, int direction);

    std::uint64_t resolve(std::uint32_t controls) const;
    const std::vector<RG1Binding>& bindings() const { return bindings_; }

private:
    std::vector<RG1Binding> bindings_;
};

} // namespace rg35xx

#endif
