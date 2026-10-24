#include "SoftKeyboard.hpp"

#include "../trs_sdl_keymap.h"

#include <cmath>
#include <SDL.h>

namespace rg35xx {

// Menu fields need host text, rather than the guest keyboard's encoded keys.
int SoftKeyboard::textSymbol() const
{
    const char* label = labelAt(selection_, shift_latched_);
    while (*label == ' ') ++label;
    if (*label && (label[1] == ' ' || label[1] == '\0'))
        return static_cast<unsigned char>(*label);
    switch (selection_) {
        case 12: return SDLK_ESCAPE;
        case 13: return shift_latched_ ? SDLK_TAB : SDLK_UP;
        case 24: return SDLK_LEFT;
        case 25: return SDLK_RIGHT;
        case 26: return SDLK_DOWN;
        case 37: return SDLK_RETURN;
        case 38: return SDLK_F9; // CLEAR: empty the field.
        case 51: return SDLK_SPACE;
        default: return 0;
    }
}

SoftKeyboard::SoftKeyboard() : selection_(14), shift_latched_(false)
{
    buildGeometry();
}

int SoftKeyboard::selection() const
{
    return selection_;
}

void SoftKeyboard::setSelection(int index)
{
    if (index >= 0 && index < keyCount())
        selection_ = index;
}

int SoftKeyboard::keyCount()
{
    return TRS_SOFT_KEY_COUNT;
}

int SoftKeyboard::rowFor(int index)
{
    return index / TRS_SOFT_KEY_COLUMNS;
}

int SoftKeyboard::columnFor(int index)
{
    return index % TRS_SOFT_KEY_COLUMNS;
}

float SoftKeyboard::keyWeight(int index)
{
    switch (index) {
        case 12: /* BREAK */
        case 13: /* UP */
        case 24: /* LEFT */
        case 25: /* RIGHT */
        case 26: /* DOWN */
        case 38: /* CLEAR */
            return 1.25f;
        case 37: /* ENTER */
            return 1.55f;
        case TRS_SOFT_KEY_SHIFT:
            return 1.65f;
        case 51: /* SPACE */
            return 2.3f;
        default:
            return 1.0f;
    }
}

void SoftKeyboard::buildGeometry()
{
    for (int row = 0; row < TRS_SOFT_KEY_ROWS; ++row) {
        float total = 0.0f;
        for (int col = 0; col < TRS_SOFT_KEY_COLUMNS; ++col)
            total += keyWeight(row * TRS_SOFT_KEY_COLUMNS + col);

        float x = 0.0f;
        for (int col = 0; col < TRS_SOFT_KEY_COLUMNS; ++col) {
            const int index = row * TRS_SOFT_KEY_COLUMNS + col;
            const float width = keyWeight(index) / total;
            geometry_[index].x = x;
            geometry_[index].width = width;
            x += width;
        }
    }
}

bool SoftKeyboard::move(NavigationDirection direction)
{
    if (direction == NavigationDirection::None)
        return false;

    const int row = rowFor(selection_);
    const int col = columnFor(selection_);
    int target = selection_;
    if (direction == NavigationDirection::Left) {
        if (col == 0)
            return false;
        target--;
    } else if (direction == NavigationDirection::Right) {
        if (col == TRS_SOFT_KEY_COLUMNS - 1)
            return false;
        target++;
    } else {
        const int target_row = row +
            (direction == NavigationDirection::Up ? -1 : 1);
        if (target_row < 0 || target_row >= TRS_SOFT_KEY_ROWS)
            return false;
        const float center = geometry_[selection_].x +
                             geometry_[selection_].width * 0.5f;
        float best_distance = 2.0f;
        for (int candidate_col = 0; candidate_col < TRS_SOFT_KEY_COLUMNS;
             ++candidate_col) {
            const int candidate = target_row * TRS_SOFT_KEY_COLUMNS +
                                  candidate_col;
            const float candidate_center = geometry_[candidate].x +
                geometry_[candidate].width * 0.5f;
            const float distance = std::fabs(candidate_center - center);
            if (distance < best_distance) {
                best_distance = distance;
                target = candidate;
            }
        }
    }

    if (target == selection_)
        return false;
    selection_ = target;
    return true;
}

int SoftKeyboard::symbol(bool shifted) const
{
    return symbolAt(selection_, shifted);
}

int SoftKeyboard::symbolAt(int index, bool shifted) const
{
    return trs_soft_key_symbol(index, shifted ? 1 : 0);
}

const char* SoftKeyboard::labelAt(int index, bool shifted) const
{
    return trs_soft_key_label(index, shifted ? 1 : 0);
}

bool SoftKeyboard::selectedIsShift() const
{
    return selection_ == TRS_SOFT_KEY_SHIFT;
}

bool SoftKeyboard::shiftLatched() const
{
    return shift_latched_;
}

void SoftKeyboard::toggleShift()
{
    shift_latched_ = !shift_latched_;
}

void SoftKeyboard::clearShift()
{
    shift_latched_ = false;
}

const KeyGeometry& SoftKeyboard::geometry(int index) const
{
    return geometry_[index];
}

} // namespace rg35xx
