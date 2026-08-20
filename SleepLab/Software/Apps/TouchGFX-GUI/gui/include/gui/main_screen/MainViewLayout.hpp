/**
 ******************************************************************************
 * @file    MainViewLayout.hpp
 * @date    20-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Where the report screen's widgets go. Pure C++17, no TouchGFX.
 ******************************************************************************
 *
 * Separated from `MainView.hpp` for one reason: `MainView.hpp` includes
 * TouchGFX and the host tests do not have TouchGFX, so as long as the layout
 * lived there nothing could assert anything about it. Two widgets shipped drawn
 * partly off a round panel, which is a failure no build catches and no test
 * could reach. These constants and `gui/common/RoundPanel.hpp` are both plain
 * headers so `Tests/RoundPanel_test.cpp` can check every rectangle against the
 * glass.
 *
 ******************************************************************************
 */

#ifndef GUI_MAIN_SCREEN_MAINVIEWLAYOUT_HPP
#define GUI_MAIN_SCREEN_MAINVIEWLAYOUT_HPP

#include <cstdint>

namespace MainViewLayout
{

/// Text rows the screen can hold. More than any mode uses; the report and the
/// history both stop well short.
constexpr int kMaxLines = 8;

/// Row pitch and box height. One line of Poppins Medium 16 with a little air,
/// and tall enough for the descenders and parentheses that "(-2 vs you)" needs.
constexpr int16_t kLineHeight = 20;

/// First text row.
///
/// 40, not 30. At y = 30 a round 240 px panel shows a 159 px chord, and the
/// first row is the honesty line -- the one that says NOT WORN or INTERRUPTED.
/// Ten pixels down buys 20 px of width, and that row needs it most.
constexpr int16_t kFirstLineY = 40;

/// The epoch strip: 100 buckets at 2 px, pinned by the message format (ledger
/// P13) and therefore the one widget that cannot be narrowed to fit. 200 px of
/// chord exists only near the vertical centre, which is what fixes kStripY --
/// at 160 its worst row leaves 201.7 px, so the margin is 1.7 px and the test
/// is not decoration.
constexpr int16_t kStripW = 200;
constexpr int16_t kStripH = 26;
constexpr int16_t kStripY = 160;

/// The caption, in the narrowest row anything is drawn in: 164 px. That number
/// is a design constraint on `RestfulnessBand::kCaption`, not a consequence of
/// it -- see the comment on that constant.
constexpr int16_t kCaptionY = 188;

} // namespace MainViewLayout

#endif // GUI_MAIN_SCREEN_MAINVIEWLAYOUT_HPP
