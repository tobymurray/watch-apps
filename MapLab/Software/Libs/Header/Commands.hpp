/**
 ******************************************************************************
 * @file    Commands.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The messages between MapLab's GUI and its Service. There are none.
 ******************************************************************************
 *
 * Every other app in this repository puts its work in the Service and its
 * pixels in the GUI, and passes messages between them. MapLab does not, and
 * the reason is the whole point of the app:
 *
 *   **What is being measured is what a renderer would pay, and a renderer runs
 *   on the GUI thread.** It decodes and rasterises between frames, shares a
 *   thread with TouchGFX, and calls `blitCopy` from inside `draw()`. A
 *   benchmark that ran in the Service would be timing a machine that no map on
 *   this device will ever run on: a thread with no framebuffer, no 10 fps tick
 *   and no widget tree competing with it.
 *
 * So the Service exists because a `Utility` app must have one, and it does
 * nothing but stay alive while the screen is open. This header exists so that
 * the next person to look for the message protocol finds this note instead of
 * concluding it was forgotten.
 *
 * If a future bench genuinely needs the Service -- an overnight power
 * measurement is the obvious candidate, and `SleepLab/Probe` is the shape it
 * would take -- this is where its messages go, packed and size-asserted
 * against the kernel pool's 256-byte block like every other app here.
 ******************************************************************************
 */

#ifndef MAPLAB_COMMANDS_HPP
#define MAPLAB_COMMANDS_HPP

namespace CustomMessage
{
// Intentionally empty. See the file comment.
} // namespace CustomMessage

#endif // MAPLAB_COMMANDS_HPP
