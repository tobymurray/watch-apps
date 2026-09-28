/**
 ******************************************************************************
 * @file    BundleReadme.hpp
 * @brief   The README.txt that travels with a dump.
 ******************************************************************************
 */

#ifndef BUNDLE_README_HPP
#define BUNDLE_README_HPP

#include "SDK/Kernel/Kernel.hpp"

namespace BundleReadme
{

constexpr char kPath[] = "README.txt";

/// @return Whether the file was written whole.
bool write(const SDK::Kernel& kernel);

} // namespace BundleReadme

#endif // BUNDLE_README_HPP
