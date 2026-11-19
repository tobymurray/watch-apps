#include "SettingsAddresses.hpp"

#include <gtest/gtest.h>

namespace
{

using SettingsAddresses::isDerived;
using SettingsAddresses::isPlausibleService;
using SettingsAddresses::NotificationServiceSite;

constexpr NotificationServiceSite kSite = {
    .backendInstance      = 0x20001000u,
    .servicePointerOffset = 0x64u,
    .phoneFlagOffset      = 21u,
    .expectedVptr         = 0x08100010u,
    .heapBase             = 0x20100000u,
    .heapEnd              = 0x20200000u,
};

TEST(NotificationServiceSite, ADefaultSiteIsNotDerived)
{
    EXPECT_FALSE(isDerived(NotificationServiceSite{}));
    EXPECT_TRUE(isDerived(kSite));
}

TEST(NotificationServiceSite, AcceptsAnAlignedPointerInsideTheHeap)
{
    EXPECT_TRUE(isPlausibleService(kSite, kSite.heapBase));
    EXPECT_TRUE(isPlausibleService(kSite, 0x20150008u));
}

TEST(NotificationServiceSite, RefusesNull)
{
    EXPECT_FALSE(isPlausibleService(kSite, 0u));
}

TEST(NotificationServiceSite, RefusesAMisalignedPointer)
{
    EXPECT_FALSE(isPlausibleService(kSite, kSite.heapBase + 2u));
}

TEST(NotificationServiceSite, RefusesAPointerOutsideTheHeap)
{
    EXPECT_FALSE(isPlausibleService(kSite, kSite.heapBase - 4u));
    EXPECT_FALSE(isPlausibleService(kSite, kSite.heapEnd));
    EXPECT_FALSE(isPlausibleService(kSite, kSite.backendInstance));
    EXPECT_FALSE(isPlausibleService(kSite, 0x08100010u));
}

TEST(NotificationServiceSite, RefusesAPointerWhoseFlagByteFallsPastTheHeap)
{
    EXPECT_FALSE(isPlausibleService(kSite, kSite.heapEnd - 20u));
    EXPECT_TRUE(isPlausibleService(kSite, kSite.heapEnd - 24u));
}

} // namespace
