//
//  Colors.h
//  NeuralAmpModeler-macOS
//
//  Created by Steven Atkinson on 12/27/22.
//
// Store the defined colors for the plugin in one place

#ifndef Colors_h
#define Colors_h

#include "IGraphicsStructs.h"

namespace PluginColors
{
// HINT: ARGB
// COLORS!
const iplug::igraphics::IColor OFF_WHITE(255, 243, 246, 249); // Material UI because Heidi said so

// From group photo
// const iplug::igraphics::IColor NAM_5(255, 206, 194, 224);  // Languid
// Lavendar const iplug::igraphics::IColor NAM_4(255, 30, 30, 39);  // Raisin
// Black const iplug::igraphics::IColor NAM_3(255, 48, 43, 96);  // Space Cadet
// const iplug::igraphics::IColor NAM_2(255, 3, 2, 1);  // Black
// const iplug::igraphics::IColor NAM_1(255, 18, 15, 18);  // Smoky Black

// Blue palette "Microsoft"
// const iplug::igraphics::IColor NAM_1(255, 13, 27, 42);  // Black Fogra 29
// const iplug::igraphics::IColor NAM_2(255, 27, 38, 59);  // Oxford Blue
// const iplug::igraphics::IColor NAM_3(255, 65, 90, 119);  // Bedazzled Blue
// const iplug::igraphics::IColor NAM_4(255, 119, 141, 169);  // Shadow Blue
// const iplug::igraphics::IColor NAM_5(224, 225, 221, 224);  // Platinum

// Dark theme
// const iplug::igraphics::IColor NAM_1(255, 26, 20, 35);  // Xiketic
// const iplug::igraphics::IColor NAM_2(255, 55, 37, 73);  // Dark Purple
// const iplug::igraphics::IColor NAM_3(255, 119, 76, 96);  // Twilight Lavendar
// const iplug::igraphics::IColor NAM_4(255, 183, 93, 105);  // Popstar
// const iplug::igraphics::IColor NAM_5(224, 234, 205, 194);  // Unbleached Silk

// const iplug::igraphics::IColor MOUSEOVER = NAM_5.WithOpacity(0.3);

// My 3 colors (purple)
// const iplug::igraphics::IColor NAM_1(255, 18, 17, 19);  // Smoky Black
// const iplug::igraphics::IColor NAM_2(255, 115, 93, 120);  // Old Lavendar
// const iplug::igraphics::IColor NAM_3(255, 189, 185, 196);  // Lavendar Gray
// Alts
// const iplug::igraphics::IColor NAM_2(255, 34, 39, 37);  // Charleston Green
// const iplug::igraphics::IColor NAM_2(255, 114, 161, 229);  // Little Boy Blue
// const iplug::igraphics::IColor NAM_3(255, 247, 247, 242);  // Baby Powder
// const iplug::igraphics::IColor NAM_3(255, 230, 220, 249);  // Pale Purple
// Pantone const iplug::igraphics::IColor NAM_3(255, 218, 203, 246);  //
// Lavender Blue

// Blue mode
const iplug::igraphics::IColor NAM_1(255, 29, 26, 31); // Raisin Black
const iplug::igraphics::IColor NAM_2(255, 80, 133, 232); // Azure
const iplug::igraphics::IColor NAM_3(255, 162, 178, 191); // Cadet Blue Crayola
// Alts
// const iplug::igraphics::IColor NAM_1(255, 18, 17, 19);  // Smoky Black
// const iplug::igraphics::IColor NAM_2(255, 126, 188, 230);  // Camel
// const iplug::igraphics::IColor NAM_2(255, 152, 202, 235);  // Pale Cerulean
// const iplug::igraphics::IColor NAM_2(255, 46, 116, 163);  // French Blue
// const iplug::igraphics::IColor NAM_2(255, 80, 171, 232);  // Blue Jeans
// const iplug::igraphics::IColor NAM_3(255, 189, 185, 196);  // Aero
// const iplug::igraphics::IColor NAM_3(255, 221, 237, 248);  // Alice Blue
// const iplug::igraphics::IColor NAM_3(255, 207, 220, 229);  // Beau Blue
// const iplug::igraphics::IColor NAM_3(255, 187, 199, 208);  // Silver Sand

// Evan Heritage theme colors
const iplug::igraphics::IColor NAM_0(0, 18, 17, 19); // Transparent

// Theme (1.48.0): the colours below are set at run time by ApplyTheme (Preferences: six palettes,
// dark or light), and the UI is rebuilt after a change, so every control picks them up when it's
// created. Defaults are the Patriot Blue dark theme, exactly the colours used before 1.48.0.
inline iplug::igraphics::IColor NAM_THEMECOLOR(255, 60, 108, 176); // Accent
inline iplug::igraphics::IColor NAM_THEMEFONTCOLOR(255, 242, 242, 242); // Text
inline iplug::igraphics::IColor NAM_ONACCENT(255, 245, 245, 245); // Text on an accent fill
inline iplug::igraphics::IColor NAM_BG(255, 26, 24, 22); // Main window background
inline iplug::igraphics::IColor NAM_CARD(255, 26, 24, 22); // Popup cards
inline iplug::igraphics::IColor NAM_PANEL(255, 20, 19, 18); // Full-page overlays, faceplates
inline iplug::igraphics::IColor NAM_SURFACE(255, 52, 51, 54); // Off-state fills (switch tracks, tabs)
inline iplug::igraphics::IColor NAM_INK(255, 255, 255, 255); // Lines, dots and wires drawn over the background
inline bool NAM_LIGHT = false;

// Misc
inline iplug::igraphics::IColor MOUSEOVER = NAM_THEMEFONTCOLOR.WithOpacity(0.1f);
inline iplug::igraphics::IColor HELP_TEXT = iplug::igraphics::COLOR_WHITE;
inline iplug::igraphics::IColor HELP_TEXT_MO = iplug::igraphics::COLOR_WHITE.WithOpacity(0.9f);
inline iplug::igraphics::IColor HELP_TEXT_CLICKED = iplug::igraphics::COLOR_WHITE.WithOpacity(0.8f);

constexpr int kNumThemes = 6;
struct ThemeInfo
{
  const char* name;
  int r, g, b;
};
inline const ThemeInfo kThemes[kNumThemes] = {
  {"Patriot Blue", 60, 108, 176}, {"Amber", 224, 146, 58}, {"Emerald", 38, 158, 108},
  {"Crimson", 200, 58, 72},       {"Violet", 132, 94, 216}, {"Teal", 30, 156, 176}};

inline iplug::igraphics::IColor ThemeAccent(int theme)
{
  const ThemeInfo& t = kThemes[theme < 0 || theme >= kNumThemes ? 0 : theme];
  return iplug::igraphics::IColor(255, t.r, t.g, t.b);
}

// `amount` of the accent mixed into a base grey.
inline iplug::igraphics::IColor ThemeTint(int r, int g, int b, const iplug::igraphics::IColor& accent, float amount)
{
  auto mix = [amount](int base, int a) { return (int)(base + (a - base) * amount + 0.5f); };
  return iplug::igraphics::IColor(255, mix(r, accent.R), mix(g, accent.G), mix(b, accent.B));
}

inline void ApplyTheme(int theme, bool light)
{
  using iplug::igraphics::IColor;
  if (theme < 0 || theme >= kNumThemes)
    theme = 0;
  const IColor accent = ThemeAccent(theme);
  NAM_THEMECOLOR = accent;
  NAM_LIGHT = light;
  NAM_ONACCENT = IColor(255, 245, 245, 245);
  if (!light)
  {
    // Patriot Blue keeps the original warm-black; the others take a faint tint of their accent.
    const float tint = theme == 0 ? 0.f : 0.05f;
    NAM_THEMEFONTCOLOR = IColor(255, 242, 242, 242);
    NAM_BG = ThemeTint(26, 24, 22, accent, tint);
    NAM_CARD = ThemeTint(26, 24, 22, accent, tint);
    NAM_PANEL = ThemeTint(20, 19, 18, accent, tint);
    NAM_SURFACE = ThemeTint(52, 51, 54, accent, tint);
    NAM_INK = IColor(255, 255, 255, 255);
  }
  else
  {
    NAM_THEMEFONTCOLOR = IColor(255, 30, 30, 34);
    NAM_BG = ThemeTint(226, 224, 220, accent, 0.06f);
    NAM_CARD = ThemeTint(242, 241, 238, accent, 0.03f);
    NAM_PANEL = ThemeTint(234, 233, 230, accent, 0.04f);
    NAM_SURFACE = ThemeTint(196, 194, 191, accent, 0.05f);
    NAM_INK = IColor(255, 24, 24, 28);
  }
  MOUSEOVER = NAM_INK.WithOpacity(0.1f);
  HELP_TEXT = NAM_THEMEFONTCOLOR;
  HELP_TEXT_MO = NAM_THEMEFONTCOLOR.WithOpacity(0.9f);
  HELP_TEXT_CLICKED = NAM_THEMEFONTCOLOR.WithOpacity(0.8f);
}

}; // namespace PluginColors

#endif /* Colors_h */
