// M0 冒烟测试：验证 gtest 工具链可用。
// M1 起公共库（EtcdClient/ODB 实体/工具类等）的单测在这里与各模块测试文件中逐步覆盖。
#include <gtest/gtest.h>

#include <string>

TEST(SmokeTest, ToolchainWorks) {
  EXPECT_EQ(1 + 1, 2);
  std::string project = "im-system";
  EXPECT_EQ(project, "im-system");
  EXPECT_FALSE(project.empty());
}
