#include <gtest/gtest.h>

#include "BootDisplayPolicy.h"

TEST(BootDisplayPolicy, X3OrdinarySleepWakeArmsCleanFirstRefresh) {
  EXPECT_FALSE(useSeamlessDisplayInit(BootResume::SplashlessWake, true, false));
}

TEST(BootDisplayPolicy, X3QuickResumeRetainsDifferentialWake) {
  EXPECT_TRUE(useSeamlessDisplayInit(BootResume::SplashlessWake, true, true));
}

TEST(BootDisplayPolicy, OtherDevicesRetainSplashlessWakeBehavior) {
  EXPECT_TRUE(useSeamlessDisplayInit(BootResume::SplashlessWake, false, false));
  EXPECT_TRUE(useSeamlessDisplayInit(BootResume::SplashlessWake, false, true));
}

TEST(BootDisplayPolicy, ExistingNonWakeRoutesRemainUnchanged) {
  EXPECT_FALSE(useSeamlessDisplayInit(BootResume::Splash, true, false));
  EXPECT_FALSE(useSeamlessDisplayInit(BootResume::Splash, false, false));
  EXPECT_TRUE(useSeamlessDisplayInit(BootResume::Silent, true, false));
  EXPECT_TRUE(useSeamlessDisplayInit(BootResume::Network, true, false));
}
