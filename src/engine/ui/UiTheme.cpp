#include "ui/UiTheme.h"
#include <string>
#include <vector>

namespace
{
Font g_font;
bool g_loaded = false;
bool g_custom = false;  // whether a custom font was loaded (it must be unloaded)

// raylib's built-in font contains ASCII only. The menu includes Russian labels, so on
// Windows use the system UI font when a project-supplied font is absent. We load ASCII
// too because this same font remains the UI font for the rest of the game and editor.
std::vector<int> UiCodepoints()
{
    std::vector<int> result;
    for (int cp = 32; cp <= 126; ++cp)
        result.push_back(cp);
    for (int cp = 0x0400; cp <= 0x052F; ++cp)  // Cyrillic, including Ё/ё
        result.push_back(cp);
    return result;
}
}  // namespace

void Ui::LoadAssets()
{
    // Prefer a project-supplied font. Without one, Windows' UI font gives the Russian
    // interface text real glyphs instead of the built-in font's missing-character boxes.
    std::string path = std::string(GetApplicationDirectory()) + "data/ui_font.ttf";
    if (FileExists(path.c_str()))
    {
        g_font = LoadFontEx(path.c_str(), 32, nullptr, 0);
        g_custom = true;
    }
    else
    {
        const char* systemFont = "C:/Windows/Fonts/segoeui.ttf";
        if (FileExists(systemFont))
        {
            std::vector<int> codepoints = UiCodepoints();
            g_font = LoadFontEx(systemFont, 32, codepoints.data(), (int)codepoints.size());
            g_custom = true;
        }
        else
        {
            g_font = GetFontDefault();
            g_custom = false;
        }
    }
    g_loaded = true;
}

void Ui::UnloadAssets()
{
    if (g_loaded && g_custom)
        UnloadFont(g_font);
}

Font Ui::GetFont()
{
    return g_loaded ? g_font : GetFontDefault();
}

void Ui::Text(const char* text, int x, int y, int size, Color color)
{
    DrawTextEx(GetFont(), text, { (float)x, (float)y }, (float)size, 1.0f, color);
}

int Ui::TextWidth(const char* text, int size)
{
    return (int)MeasureTextEx(GetFont(), text, (float)size, 1.0f).x;
}
