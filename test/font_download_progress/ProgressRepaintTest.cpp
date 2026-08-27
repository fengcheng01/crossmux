#include <gtest/gtest.h>

#include "util/ProgressRepaint.h"

TEST(ProgressRepaint, PaintsFirstTenPercentAndCompletion) {
  EXPECT_FALSE(shouldRepaintProgress(0, 9, 0, 0));
  EXPECT_TRUE(shouldRepaintProgress(0, 10, 0, 0));
  EXPECT_TRUE(shouldRepaintProgress(90, 100, 0, 0));
}

TEST(ProgressRepaint, PaintsWhenIntervalElapsedEvenIfPercentBarelyMoved) {
  EXPECT_FALSE(shouldRepaintProgress(0, 1, 1000, 1499));
  EXPECT_TRUE(shouldRepaintProgress(0, 1, 1000, 1500));
}
