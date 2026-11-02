#include "config.h"

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "logger.h"

namespace daikon {
namespace {

std::optional<Config> Parse(std::initializer_list<std::string_view> options) {
    std::vector<std::string_view> args{"daikon"};
    args.insert(args.end(), options);
    return ParseArgs(args);
}

TEST(ConfigExitTest, HelpAndVersionRequestExitWithoutServing) {
    for (const std::string_view option : {"-h", "--help", "-v", "--version"}) {
        EXPECT_FALSE(Parse({option}).has_value()) << "option: " << option;
    }
}

TEST(ConfigBindTest, DefaultsToNoExplicitAddresses) {
    const auto config = Parse({});

    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->bind_addresses.empty());
}

TEST(ConfigBindTest, RequiresValue) {
    EXPECT_THROW(Parse({"--bind"}), std::invalid_argument);
}

TEST(ConfigBindTest, AcceptsLongAndShortOptions) {
    const auto long_form = Parse({"--bind", "127.0.0.1"});
    const auto short_form = Parse({"-b", "127.0.0.2"});

    ASSERT_TRUE(long_form.has_value());
    EXPECT_EQ(long_form->bind_addresses,
              (std::vector<std::string>{"127.0.0.1"}));

    ASSERT_TRUE(short_form.has_value());
    EXPECT_EQ(short_form->bind_addresses,
              (std::vector<std::string>{"127.0.0.2"}));
}

TEST(ConfigBindTest, AcceptsIpv4Address) {
    const auto config = Parse({"--bind", "127.0.0.1"});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->bind_addresses, (std::vector<std::string>{"127.0.0.1"}));
}

TEST(ConfigBindTest, AcceptsIpv6Address) {
    const auto config = Parse({"--bind", "::1"});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->bind_addresses, (std::vector<std::string>{"::1"}));
}

TEST(ConfigBindTest, AccumulatesRepeatedOptionsInOrder) {
    const auto config = Parse({"--bind", "127.0.0.1", "-b", "::1"});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->bind_addresses,
              (std::vector<std::string>{"127.0.0.1", "::1"}));
}

TEST(ConfigBindTest, AcceptsWildcardAddresses) {
    const auto config = Parse({"-b", "0.0.0.0", "-b", "::"});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->bind_addresses,
              (std::vector<std::string>{"0.0.0.0", "::"}));
}

TEST(ConfigBindTest, RejectsInvalidAddresses) {
    for (const std::string_view address :
         {"localhost", "", "127.0.0", "256.0.0.1", "::g", "[::1]",
          "127.0.0.1:6379", " 127.0.0.1"}) {

        EXPECT_THROW(Parse({"--bind", address}), std::invalid_argument)
            << "address: '" << address << "'";
    }
}

TEST(ConfigPortTest, DefaultsTo6379) {
    const auto config = Parse({});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->port, 6379);
}

TEST(ConfigPortTest, RequiresValue) {
    EXPECT_THROW(Parse({"--port"}), std::invalid_argument);
}

TEST(ConfigPortTest, AcceptsLongAndShortOptions) {
    const auto long_form = Parse({"--port", "7000"});
    const auto short_form = Parse({"-p", "7001"});

    ASSERT_TRUE(long_form.has_value());
    EXPECT_EQ(long_form->port, 7000);

    ASSERT_TRUE(short_form.has_value());
    EXPECT_EQ(short_form->port, 7001);
}

TEST(ConfigPortTest, RejectsZeroPort) {
    EXPECT_THROW(Parse({"--port", "0"}), std::invalid_argument);
}

TEST(ConfigPortTest, AcceptsBoundaryValues) {
    const auto lowest = Parse({"--port", "1"});
    const auto highest = Parse({"--port", "65535"});

    ASSERT_TRUE(lowest.has_value());
    EXPECT_EQ(lowest->port, 1);

    ASSERT_TRUE(highest.has_value());
    EXPECT_EQ(highest->port, 65535);
}

TEST(ConfigPortTest, RejectsInvalidPorts) {
    for (const std::string_view port :
         {"65536", "-1", "+80", "abc", "", "80x", " 80", "99999999999"}) {

        EXPECT_THROW(Parse({"--port", port}), std::invalid_argument)
            << "port: '" << port << "'";
    }
}

TEST(ConfigPortTest, LastOccurrenceWins) {
    const auto config = Parse({"--port", "7000", "-p", "7001"});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->port, 7001);
}

TEST(ConfigLogLevelTest, DefaultsToInfo) {
    const auto config = Parse({});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->log_level, LogLevel::kInfo);
}

TEST(ConfigLogLevelTest, RequiresValue) {
    EXPECT_THROW(Parse({"--log-level"}), std::invalid_argument);
}

TEST(ConfigLogLevelTest, AcceptsLongAndShortOptions) {
    const auto long_form = Parse({"--log-level", "debug"});
    const auto short_form = Parse({"-l", "info"});

    ASSERT_TRUE(long_form.has_value());
    EXPECT_EQ(long_form->log_level, LogLevel::kDebug);

    ASSERT_TRUE(short_form.has_value());
    EXPECT_EQ(short_form->log_level, LogLevel::kInfo);
}

TEST(ConfigLogLevelTest, AcceptsAllLevelNames) {
    const std::array<std::pair<std::string_view, LogLevel>, 5> levels{{
        {"debug", LogLevel::kDebug},
        {"info", LogLevel::kInfo},
        {"notice", LogLevel::kNotice},
        {"warning", LogLevel::kWarning},
        {"error", LogLevel::kErr},
    }};

    for (const auto& [name, level] : levels) {
        const auto config = Parse({"--log-level", name});

        ASSERT_TRUE(config.has_value()) << "level: " << name;
        EXPECT_EQ(config->log_level, level) << "level: " << name;
    }
}

TEST(ConfigLogLevelTest, RejectsInvalidLevels) {
    for (const std::string_view level :
         {"err", "critical", "crit", "alert", "emergency", "emerg", "",
          "verbose", "INFO", " info", "0"}) {

        EXPECT_THROW(Parse({"--log-level", level}), std::invalid_argument)
            << "level: '" << level << "'";
    }
}

TEST(ConfigLogLevelTest, LastOccurrenceWins) {
    const auto config = Parse({"--log-level", "debug", "-l", "error"});

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->log_level, LogLevel::kErr);
}

TEST(ConfigUnknownOptionTest, RejectsUnknownOptions) {
    for (const std::string_view option :
         {"--unknown", "-x", "-", "--", "positional"}) {

        EXPECT_THROW(Parse({option}), std::invalid_argument)
            << "option: '" << option << "'";
    }
}

TEST(ConfigUnknownOptionTest, RejectsUnknownOptionAfterValidOnes) {
    EXPECT_THROW(Parse({"--port", "7000", "--unknown"}), std::invalid_argument);
}

}  // namespace
}  // namespace daikon
