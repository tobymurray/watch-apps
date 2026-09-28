#include <gtest/gtest.h>

#include "ReadGate.hpp"

using ReadGate::Verdict;

namespace {

constexpr uint32_t kPrivileged   = 0x00000000u;
constexpr uint32_t kUnprivileged = 0x00000001u;
constexpr uint32_t kMpuOff       = 0x00000000u;
constexpr uint32_t kMpuOn        = 0x00000001u;
constexpr uint32_t kTzOff        = 0x000000AAu;
constexpr uint32_t kTzOn         = 0x800000AAu;

} // namespace

TEST(ReadGate, AllowsTheRegistersFirmware130Read)
{
    // CONTROL=0x00000006 and MPU_CTRL=0 from the 2026-07-29 investigation's
    // sweep #3; RDP 0xAA with TZEN clear.
    EXPECT_EQ(ReadGate::decide(0x00000006u, 0x00000000u, kTzOff), Verdict::Allowed);
}

TEST(ReadGate, RefusesEachIsolationBitOnItsOwn)
{
    EXPECT_EQ(ReadGate::decide(kUnprivileged, kMpuOff, kTzOff), Verdict::Unprivileged);
    EXPECT_EQ(ReadGate::decide(kPrivileged, kMpuOn, kTzOff), Verdict::MpuEnabled);
    EXPECT_EQ(ReadGate::decide(kPrivileged, kMpuOff, kTzOn), Verdict::TrustZone);
}

TEST(ReadGate, NamesTheFirstBitInReadOrder)
{
    // Order matters: an unprivileged thread never gets to read MPU_CTRL, so
    // what it would have said cannot be the reason given.
    EXPECT_EQ(ReadGate::decide(kUnprivileged, kMpuOn, kTzOn), Verdict::Unprivileged);
    EXPECT_EQ(ReadGate::decide(kPrivileged, kMpuOn, kTzOn), Verdict::MpuEnabled);
}

TEST(ReadGate, IgnoresBitsThatDoNotDecideWhetherAReadFaults)
{
    // SPSEL and FPCA in CONTROL, PRIVDEFENA and HFNMIENA in MPU_CTRL, DUALBANK
    // and every other option bit in FLASH_OPTR.
    EXPECT_EQ(ReadGate::decide(0x00000006u, 0x00000006u, 0x7FFFFFFFu), Verdict::Allowed);
}

TEST(ReadGate, NamesTheBlockingBit)
{
    EXPECT_STREQ(ReadGate::blockingBit(Verdict::Allowed), "none");
    EXPECT_STREQ(ReadGate::blockingBit(Verdict::Unprivileged), "CONTROL.nPRIV");
    EXPECT_STREQ(ReadGate::blockingBit(Verdict::MpuEnabled), "MPU_CTRL.ENABLE");
    EXPECT_STREQ(ReadGate::blockingBit(Verdict::TrustZone), "FLASH_OPTR.TZEN");
}
