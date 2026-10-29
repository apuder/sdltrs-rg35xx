#include "SoftKeyboardRenderer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace rg35xx {
namespace {

enum class ArrowGlyph {
    None,
    Up,
    Down,
    Left,
    Right,
};

/* Original compact 5x7 bitmap glyphs, distributed under this project's
 * BSD-2-Clause license. Each low five bits describe one scanline. */
void glyphRows(char input, unsigned char rows[7])
{
    std::fill(rows, rows + 7, 0);
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
#define G(a,b,c,d,e,f,g) do { rows[0]=a; rows[1]=b; rows[2]=c; rows[3]=d; rows[4]=e; rows[5]=f; rows[6]=g; } while (0)
    switch (c) {
        case 'A': G(14,17,17,31,17,17,17); break;
        case 'B': G(30,17,17,30,17,17,30); break;
        case 'C': G(14,17,16,16,16,17,14); break;
        case 'D': G(30,17,17,17,17,17,30); break;
        case 'E': G(31,16,16,30,16,16,31); break;
        case 'F': G(31,16,16,30,16,16,16); break;
        case 'G': G(14,17,16,23,17,17,15); break;
        case 'H': G(17,17,17,31,17,17,17); break;
        case 'I': G(14,4,4,4,4,4,14); break;
        case 'J': G(7,2,2,2,2,18,12); break;
        case 'K': G(17,18,20,24,20,18,17); break;
        case 'L': G(16,16,16,16,16,16,31); break;
        case 'M': G(17,27,21,21,17,17,17); break;
        case 'N': G(17,25,21,19,17,17,17); break;
        case 'O': G(14,17,17,17,17,17,14); break;
        case 'P': G(30,17,17,30,16,16,16); break;
        case 'Q': G(14,17,17,17,21,18,13); break;
        case 'R': G(30,17,17,30,20,18,17); break;
        case 'S': G(15,16,16,14,1,1,30); break;
        case 'T': G(31,4,4,4,4,4,4); break;
        case 'U': G(17,17,17,17,17,17,14); break;
        case 'V': G(17,17,17,17,17,10,4); break;
        case 'W': G(17,17,17,21,21,21,10); break;
        case 'X': G(17,17,10,4,10,17,17); break;
        case 'Y': G(17,17,10,4,4,4,4); break;
        case 'Z': G(31,1,2,4,8,16,31); break;
        case '0': G(14,17,19,21,25,17,14); break;
        case '1': G(4,12,4,4,4,4,14); break;
        case '2': G(14,17,1,2,4,8,31); break;
        case '3': G(30,1,1,14,1,1,30); break;
        case '4': G(2,6,10,18,31,2,2); break;
        case '5': G(31,16,16,30,1,1,30); break;
        case '6': G(14,16,16,30,17,17,14); break;
        case '7': G(31,1,2,4,8,8,8); break;
        case '8': G(14,17,17,14,17,17,14); break;
        case '9': G(14,17,17,15,1,1,14); break;
        case ' ': break;
        case '!': G(4,4,4,4,4,0,4); break;
        case '"': G(10,10,10,0,0,0,0); break;
        case '#': G(10,31,10,10,31,10,0); break;
        case '$': G(4,15,20,14,5,30,4); break;
        case '%': G(25,25,2,4,8,19,19); break;
        case '&': G(12,18,20,8,21,18,13); break;
        case '\'': G(4,4,8,0,0,0,0); break;
        case '(': G(2,4,8,8,8,4,2); break;
        case ')': G(8,4,2,2,2,4,8); break;
        case '*': G(0,21,14,31,14,21,0); break;
        case '+': G(0,4,4,31,4,4,0); break;
        case ',': G(0,0,0,0,4,4,8); break;
        case '-': G(0,0,0,31,0,0,0); break;
        case '.': G(0,0,0,0,0,4,4); break;
        case '/': G(1,2,2,4,8,8,16); break;
        case ':': G(0,4,4,0,4,4,0); break;
        case ';': G(0,4,4,0,4,4,8); break;
        case '<': G(2,4,8,16,8,4,2); break;
        case '=': G(0,0,31,0,31,0,0); break;
        case '>': G(8,4,2,1,2,4,8); break;
        case '?': G(14,17,1,2,4,0,4); break;
        case '@': G(14,17,23,21,23,16,14); break;
        case '[': G(14,8,8,8,8,8,14); break;
        case '\\': G(16,8,8,4,2,2,1); break;
        case ']': G(14,2,2,2,2,2,14); break;
        case '^': G(4,10,17,0,0,0,0); break;
        case '_': G(0,0,0,0,0,0,31); break;
        case '{': G(2,4,4,8,4,4,2); break;
        case '|': G(4,4,4,4,4,4,4); break;
        case '}': G(8,4,4,2,4,4,8); break;
        case '~': G(0,0,9,22,0,0,0); break;
        default: G(31,17,17,17,17,17,31); break;
    }
#undef G
}

std::string trimLabel(const char* label)
{
    std::string value(label ? label : "");
    while (!value.empty() && value[0] == ' ')
        value.erase(value.begin());
    while (!value.empty() && value[value.size() - 1] == ' ')
        value.erase(value.end() - 1);
    return value;
}

ArrowGlyph arrowForLabel(const std::string& label)
{
    if (label == "UP") return ArrowGlyph::Up;
    if (label == "DWN") return ArrowGlyph::Down;
    if (label == "LFT") return ArrowGlyph::Left;
    if (label == "RGT") return ArrowGlyph::Right;
    return ArrowGlyph::None;
}

void drawBitmapGlyph(SDL_Renderer* renderer, const unsigned char rows[7],
                     int x, int y, int max_width, int max_height,
                     int max_scale = 3)
{
    int scale = std::min(max_width / 5, max_height / 7);
    scale = std::max(1, std::min(scale, max_scale));
    const int width = 5 * scale;
    const int height = 7 * scale;
    const int start_x = x + (max_width - width) / 2;
    const int start_y = y + (max_height - height) / 2;
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((rows[row] & (1u << (4 - col))) != 0) {
                SDL_Rect pixel = {start_x + col * scale,
                                  start_y + row * scale,
                                  scale,
                                  scale};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
}

void drawArrowGlyph(SDL_Renderer* renderer, ArrowGlyph glyph, int x, int y,
                    int max_width, int max_height, int max_scale = 2)
{
    static const unsigned char up_rows[7] = {4, 14, 21, 4, 4, 4, 0};
    static const unsigned char down_rows[7] = {0, 4, 4, 4, 21, 14, 4};
    static const unsigned char left_rows[7] = {2, 4, 31, 4, 2, 0, 0};
    static const unsigned char right_rows[7] = {8, 4, 31, 4, 8, 0, 0};
    switch (glyph) {
        case ArrowGlyph::Up:
            drawBitmapGlyph(renderer, up_rows, x, y, max_width, max_height, max_scale);
            break;
        case ArrowGlyph::Down:
            drawBitmapGlyph(renderer, down_rows, x, y, max_width, max_height, max_scale);
            break;
        case ArrowGlyph::Left:
            drawBitmapGlyph(renderer, left_rows, x, y, max_width, max_height, max_scale);
            break;
        case ArrowGlyph::Right:
            drawBitmapGlyph(renderer, right_rows, x, y, max_width, max_height, max_scale);
            break;
        case ArrowGlyph::None:
            break;
    }
}

void drawText(SDL_Renderer* renderer, const std::string& text, int x, int y,
              int max_width, int max_height, int max_scale = 3)
{
    if (text.empty())
        return;
    int scale = std::min(max_width / static_cast<int>(text.size() * 6),
                         max_height / 7);
    scale = std::max(1, std::min(scale, max_scale));
    const int width = static_cast<int>(text.size()) * 6 * scale - scale;
    const int height = 7 * scale;
    int cursor_x = x + (max_width - width) / 2;
    const int cursor_y = y + (max_height - height) / 2;
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned char rows[7];
        glyphRows(text[i], rows);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) != 0) {
                    SDL_Rect pixel = {cursor_x + col * scale,
                                      cursor_y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

int textScale(const std::string& text, int max_width, int max_height,
              int max_scale = 3)
{
    if (text.empty())
        return 1;
    int scale = std::min(max_width / static_cast<int>(text.size() * 6),
                         max_height / 7);
    return std::max(1, std::min(scale, max_scale));
}

} // namespace

void SoftKeyboardRenderer::draw(SDL_Renderer* renderer,
                                const SoftKeyboard& keyboard, bool text_entry) const
{
    if (renderer == NULL)
        return;

    Uint8 saved_r = 0, saved_g = 0, saved_b = 0, saved_a = 0;
    SDL_BlendMode saved_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawColor(renderer, &saved_r, &saved_g, &saved_b, &saved_a);
    SDL_GetRenderDrawBlendMode(renderer, &saved_blend);

    int width = 0, height = 0;
    SDL_RenderGetLogicalSize(renderer, &width, &height);
    if (width <= 0 || height <= 0)
        SDL_GetRendererOutputSize(renderer, &width, &height);
    if (width <= 0 || height <= 0) {
        SDL_SetRenderDrawBlendMode(renderer, saved_blend);
        SDL_SetRenderDrawColor(renderer, saved_r, saved_g, saved_b, saved_a);
        return;
    }

    const int panel_width = std::min(width, std::max(240, width * 2 / 3));
    const int panel_x = (width - panel_width) / 2;
    const int panel_height = std::min(height, std::max(150, height * 50 / 100));
    const int panel_y = std::max(0, height - panel_height);
    const int padding = std::max(4, panel_width / 100);
    const int header_height = std::max(14, panel_height / 11);
    const int legend_height = std::max(14, panel_height / 10);
    const int gap = std::max(2, panel_height / 110);
    const int keyboard_y = panel_y + header_height;
    const int keyboard_height = std::max(4,
        panel_height - header_height - legend_height - padding * 2);
    const int row_height = (keyboard_height - gap * 3) / 4;
    const int keyboard_width = panel_width - padding * 2;
    const std::string legend_text =
        text_entry ? "B TYPE  Y ERASE  START OK  SELECT CANCEL"
                   : "SELECT CLOSE  B TYPE  A ENTER  X SHIFT";
    const int legend_text_scale =
        textScale(legend_text, panel_width - padding * 2, legend_height - 2);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 8, 12, 18, 78);
    SDL_Rect panel = {panel_x, panel_y, panel_width, panel_height};
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer,
        keyboard.shiftLatched() ? 255 : 190,
        keyboard.shiftLatched() ? 176 : 190,
        keyboard.shiftLatched() ? 38 : 190, 255);
    drawText(renderer, keyboard.shiftLatched() ? "SHIFT ON" : "SHIFT OFF",
             panel_x + padding, panel_y + 1, panel_width - 2 * padding,
             header_height - 2, legend_text_scale);

    for (int index = 0; index < SoftKeyboard::keyCount(); ++index) {
        const int row = SoftKeyboard::rowFor(index);
        const KeyGeometry& geometry = keyboard.geometry(index);
        int x = panel_x + padding + static_cast<int>(geometry.x * keyboard_width);
        const int next_x = panel_x + padding + static_cast<int>(
            (geometry.x + geometry.width) * keyboard_width);
        SDL_Rect key = {x + gap / 2,
                        keyboard_y + row * (row_height + gap),
                        std::max(2, next_x - x - gap), row_height};
        const bool selected = index == keyboard.selection();
        if (selected)
            SDL_SetRenderDrawColor(renderer, 238, 128, 30, 132);
        else
            SDL_SetRenderDrawColor(renderer, 18, 24, 32, 58);
        SDL_RenderFillRect(renderer, &key);

        SDL_SetRenderDrawColor(renderer,
            selected ? 255 : 186, selected ? 240 : 196,
            selected ? 170 : 210, 255);
        SDL_RenderDrawRect(renderer, &key);
        std::string label = trimLabel(keyboard.labelAt(index, keyboard.shiftLatched()));
        const ArrowGlyph arrow = arrowForLabel(label);
        if (arrow != ArrowGlyph::None) {
            drawArrowGlyph(renderer, arrow,
                           key.x + 2, key.y + 2,
                           std::max(1, key.w - 4),
                           std::max(1, key.h - 4), 2);
        } else {
            if (label == "SPC")
                label = "SPACE";
            drawText(renderer, label,
                     key.x + 2, key.y + 2, std::max(1, key.w - 4),
                     std::max(1, key.h - 4), 2);
        }
    }

    SDL_SetRenderDrawColor(renderer, 225, 230, 238, 255);
    drawText(renderer, legend_text,
             panel_x + padding, panel_y + panel_height - legend_height,
             panel_width - padding * 2,
             legend_height - 2);

    SDL_SetRenderDrawBlendMode(renderer, saved_blend);
    SDL_SetRenderDrawColor(renderer, saved_r, saved_g, saved_b, saved_a);
}

} // namespace rg35xx
