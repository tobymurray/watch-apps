/**
 ******************************************************************************
 * @file    RegisterSweep.hpp
 * @brief   A labelled, repeatable read of the peripheral registers.
 ******************************************************************************
 *
 * FwDump takes one sweep, once, at app start, to record what a flash image
 * cannot say about itself. This app needs the same reads taken *many* times, at
 * moments it controls, and kept apart so they can be diffed against each other.
 * That is the whole experiment: if the pin driving the front-light is a plain
 * output, the dark and lit sweeps differ in one `ODR` bit and nowhere else; if
 * it is a timer channel, they differ in `MODER`/`AFR` and a `CCRx` moves with
 * the requested level.
 *
 * So each sweep goes to **its own file**, `sweep_<label>.txt`, in the exact
 * address-labelled form the 2026-07-29 investigation used:
 *
 *     SWP RCC       46020C00: 00000063 00000000 00001000 22000000
 *
 * Which means two of them diff line-for-line with plain `diff`, and each decodes
 * with that investigation's existing Python, with no extraction step in between.
 * A single combined file would have needed both.
 *
 * ## Everything here is a read
 *
 * Loads from memory-mapped peripheral space, and writes only into the app's own
 * sandbox. Nothing in this file writes a peripheral register. In particular it
 * does not walk the MPU region table, which would need a write to `MPU_RNR` --
 * FwDump draws that line deliberately and this app holds it.
 *
 * ## The timer blocks are opt-in, and that is not timidity
 *
 * Every base in the default set was read successfully on this unit by the prior
 * investigation. The timer bases have **not** been. They are inferred from the
 * classic STM32 APB1/APB2 layout, which is consistent with the I2C and SPI bases
 * already in the set but is not the same thing as having been confirmed against
 * RM0456 for this device group.
 *
 * That matters more here than it would elsewhere, because internal flash and
 * peripheral space are memory-mapped: a "read" is a pointer dereference, there
 * is no call that can fail, and an address that does not decode raises a
 * BusFault which escalates to a HardFault and takes the app down. There is no
 * recovering from it in-app.
 *
 * So: the default sweep is the confirmed set, and the timer blocks are enabled
 * by a config file. Run once without them to get the answer that matters: the
 * GPIO diff already says whether the pin is an output or an alternate function,
 * which is the coarse form of the question, and enable them for a second run
 * once someone has checked the bases against RM0456.
 *
 * The blocks are also written in order, each flushed before the next, so a run
 * that does fault has already committed everything up to the block that killed
 * it. The last block named in the file is then the one to blame, which is the
 * only diagnostic available for a fault that cannot be caught.
 *
 ******************************************************************************
 */

#ifndef REGISTER_SWEEP_HPP
#define REGISTER_SWEEP_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace RegisterSweep
{

/// True on builds where these registers exist to be read. False on the host and
/// on the simulator, where the addresses are not mapped and dereferencing them
/// would end the process rather than read anything.
bool available();

/**
 * @brief Write one labelled sweep to `sweep_<label>.txt`.
 *
 * @param label        Short, filename-safe. Becomes part of the path, so the
 *                     caller is responsible for it being sane; the plan's
 *                     labels are all lowercase and underscored.
 * @param includeTimers Read the unconfirmed timer bases as well. See the file
 *                     comment before setting this.
 * @return Whether the file was written whole. False on a host build, where
 *         there is nothing to sweep and pretending otherwise would put zeros on
 *         record as if they were measurements.
 *
 * Streamed line by line into the open file rather than accumulated: a sweep is
 * a few kilobytes, which is more than a service stack wants to hold, and
 * nothing parses this with a regex that a truncated tail could mislead.
 */
bool write(const SDK::Kernel& kernel, const char* label, bool includeTimers);

/// How many blocks the default (confirmed) set contains. Reported into the
/// results file so a reader knows what coverage a sweep claims.
size_t confirmedBlockCount();

/// How many blocks the timer set adds.
size_t timerBlockCount();

} // namespace RegisterSweep

#endif // REGISTER_SWEEP_HPP
