#include <fstream>
#include <gtest/gtest.h>

#include "Domain/ScmpException.h"
#include "Utils/ConfigurationUtils.h"
#include "Utils/Utils.h"

namespace
{
class ConfigurationTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        const int fd = ::mkstemp(path);
        ASSERT_GE(fd, 0);
        ::close(fd);
        std::ofstream config(path);
        config << "# Keep this comment\nvisualizer.fps=20\ncustom.path=a=b\n";
    }

    void TearDown() override
    {
        ::unlink(path);
    }

    char path[64] = "/tmp/scmp-config-test.XXXXXX";
};
}

TEST_F(ConfigurationTest, SavesAndReloadsWithoutDroppingExistingConfig)
{
    scmp::ConfigurationUtils::save_options(path, {{"visualizer.fps", "030"},
                                                {"visualizers", "lava,spectrum"}});
    scmp::Settings settings;
    scmp::ConfigurationUtils::load_settings(settings, path);
    EXPECT_EQ(settings.get_fps(), 30u);
    EXPECT_EQ(settings.get_visualizers(), (std::vector<std::string>{"lava", "spectrum"}));
    EXPECT_EQ(scmp::ConfigurationUtils::read_config(path).at("custom.path"), "a=b");
    std::string first_line;
    std::getline(std::ifstream(path), first_line);
    EXPECT_EQ(first_line, "# Keep this comment");
}

TEST_F(ConfigurationTest, RejectsTheEntireBatchWhenOneSettingIsInvalid)
{
    EXPECT_THROW(scmp::ConfigurationUtils::save_options(
                     path, {{"visualizer.fps", "0"}, {"visualizers", "lava"}}),
                 scmp::ScmpException);
    const auto values = scmp::ConfigurationUtils::read_config(path);
    EXPECT_EQ(values.at("visualizer.fps"), "20");
    EXPECT_EQ(values.count("visualizers"), 0u);
}
