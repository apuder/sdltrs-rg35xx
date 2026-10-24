#ifndef RG35XX_SOFT_KEYBOARD_HPP
#define RG35XX_SOFT_KEYBOARD_HPP

#include <cstddef>

namespace rg35xx {

enum class NavigationDirection {
    None,
    Up,
    Down,
    Left,
    Right
};

struct KeyGeometry {
    float x;
    float width;
};

class SoftKeyboard {
public:
    SoftKeyboard();

    int selection() const;
    void setSelection(int index);
    bool move(NavigationDirection direction);
    int symbol(bool shifted) const;
    int symbolAt(int index, bool shifted) const;
    int textSymbol() const;
    const char* labelAt(int index, bool shifted) const;
    bool selectedIsShift() const;
    bool shiftLatched() const;
    void toggleShift();
    void clearShift();
    const KeyGeometry& geometry(int index) const;

    static int keyCount();
    static int rowFor(int index);
    static int columnFor(int index);

private:
    static float keyWeight(int index);
    void buildGeometry();

    int selection_;
    bool shift_latched_;
    KeyGeometry geometry_[52];
};

} // namespace rg35xx

#endif
