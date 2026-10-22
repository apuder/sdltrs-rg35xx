#ifndef RG35XX_SOFT_KEYBOARD_RENDERER_HPP
#define RG35XX_SOFT_KEYBOARD_RENDERER_HPP

#include "SoftKeyboard.hpp"

#include <SDL.h>

namespace rg35xx {

class SoftKeyboardRenderer {
public:
    void draw(SDL_Renderer* renderer, const SoftKeyboard& keyboard) const;
};

} // namespace rg35xx

#endif
