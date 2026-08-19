/**
 * Host tests for reading the home position off the watch's own storage.
 *
 * This is the wiring test, and it is here because of what happened to
 * SleepLab: every part of that app's glance built, and its own tests passed,
 * and it displayed nothing on hardware for weeks because the pieces were joined
 * up wrongly in a way no compiler and no unit test could see. A query path with
 * a typo in it fails exactly like that -- the app starts, the file is there, and
 * the screen says the position is not set.
 *
 * So the documents below are the real thing: the exact bytes Kira's assembler
 * produces from a manifest declaring `values.lat` and `values.lon`, quotes and
 * indentation included, rather than a JSON snippet written to suit the parser.
 */

#include <gtest/gtest.h>

#include <string>

#include "KernelTestDoubles.hpp"

#include "HomeConfig.hpp"

namespace {

using SDK::TestSupport::KernelFixture;
using Sun::HomeConfig;

constexpr char kPath[] = "input.json";

/// What Kira writes: two-space indent, schema first, every value a string.
/// Copied from the shape `kira-core::config::document()` builds, because the
/// point of this fixture is to be that and not something equivalent.
std::string kiraDocument(const std::string &lat, const std::string &lon)
{
    return "{\n"
           "  \"schema\": 1,\n"
           "  \"values\": {\n"
           "    \"lat\": \"" + lat + "\",\n"
           "    \"lon\": \"" + lon + "\"\n"
           "  }\n"
           "}\n";
}

struct HomeConfigTest : public ::testing::Test {
    KernelFixture fx;

    HomeConfig::Status statusFor(const std::string &document)
    {
        fx.fileSystem.seedFile(kPath, document);
        HomeConfig config(fx.kernel, kPath);
        config.refresh();
        return config.status();
    }
};

TEST_F(HomeConfigTest, ReadsWhatKiraWrites)
{
    fx.fileSystem.seedFile(kPath, kiraDocument("45.4215", "-75.6972"));

    HomeConfig config(fx.kernel, kPath);
    EXPECT_TRUE(config.refresh());

    EXPECT_EQ(config.status(), HomeConfig::Status::Ok);
    ASSERT_TRUE(config.fix().has());
    EXPECT_EQ(config.fix().source, Sun::Fix::Source::Config);
    EXPECT_NEAR(config.fix().latDeg, 45.4215, 1e-9);
    EXPECT_NEAR(config.fix().lonDeg, -75.6972, 1e-9);
    EXPECT_EQ(config.fix().utc, -1) << "a configured home is timeless, not fresh";
}

TEST_F(HomeConfigTest, NoFileIsNotAPositionAndNotAnError)
{
    HomeConfig config(fx.kernel, kPath);
    config.refresh();

    EXPECT_EQ(config.status(), HomeConfig::Status::Absent);
    EXPECT_FALSE(config.fix().has());
    // The Gulf of Guinea test: nothing may fall back to (0, 0), which is a real
    // place with a plausible sunrise.
    EXPECT_DOUBLE_EQ(config.fix().latDeg, 0.0);
    EXPECT_DOUBLE_EQ(config.fix().lonDeg, 0.0);
    EXPECT_EQ(config.fix().source, Sun::Fix::Source::None);
}

TEST_F(HomeConfigTest, AFileThatIsNotWorkingIsDistinguishedFromNoFile)
{
    // Two failures, two things to do about them, so they are never collapsed.
    EXPECT_EQ(statusFor("{ not json"), HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor(""), HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor("{\n  \"schema\": 2,\n  \"values\": { \"lat\": \"45\", \"lon\": \"-75\" }\n}\n"),
              HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor("{\n  \"values\": { \"lat\": \"45\", \"lon\": \"-75\" }\n}\n"),
              HomeConfig::Status::Rejected);
}

TEST_F(HomeConfigTest, HalfAPositionIsNoPosition)
{
    EXPECT_EQ(statusFor("{\n  \"schema\": 1,\n  \"values\": { \"lat\": \"45.4215\" }\n}\n"),
              HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor("{\n  \"schema\": 1,\n  \"values\": { \"lon\": \"-75.6972\" }\n}\n"),
              HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor("{\n  \"schema\": 1,\n  \"values\": {}\n}\n"),
              HomeConfig::Status::Rejected);
}

TEST_F(HomeConfigTest, AValueThatWouldParseWronglyIsRefused)
{
    // Each of these would have produced a believable screen under a lenient
    // reader, which is the only kind of bug this app can really have.
    EXPECT_EQ(statusFor(kiraDocument("45,4215", "-75,6972")), HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor(kiraDocument("45.4215N", "75.6972W")), HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor(kiraDocument("91", "-75.6972")), HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor(kiraDocument("45.4215", "-181")), HomeConfig::Status::Rejected);
    EXPECT_EQ(statusFor(kiraDocument("", "")), HomeConfig::Status::Rejected);
}

TEST_F(HomeConfigTest, TheKeysAreTheOnesTheManifestDeclares)
{
    // Names the failure this file exists to prevent: the app reading one key
    // and the registry manifest writing another. If either moves, this fails
    // here rather than on somebody's wrist.
    EXPECT_STREQ(Sun::kLatQuery, "values.lat");
    EXPECT_STREQ(Sun::kLonQuery, "values.lon");

    fx.fileSystem.seedFile(kPath, kiraDocument("51.5074", "-0.1278"));
    HomeConfig config(fx.kernel, kPath);
    config.refresh();
    ASSERT_EQ(config.status(), HomeConfig::Status::Ok);
    EXPECT_NEAR(config.fix().latDeg, 51.5074, 1e-9);
}

TEST_F(HomeConfigTest, AnOversizedFileIsRefusedBeforeItIsRead)
{
    std::string huge = "{\n  \"schema\": 1,\n  \"values\": { \"lat\": \"45\", \"lon\": \"-75\" },\n  \"pad\": \"";
    huge.append(InputConfig::kMaxFileBytes, 'x');
    huge += "\"\n}\n";

    EXPECT_EQ(statusFor(huge), HomeConfig::Status::Rejected);
}

TEST_F(HomeConfigTest, RefreshIsQuietUntilSomethingChanges)
{
    fx.fileSystem.seedFile(kPath, kiraDocument("45.4215", "-75.6972"));

    HomeConfig config(fx.kernel, kPath);
    EXPECT_TRUE(config.refresh()) << "the first look always reads";
    EXPECT_FALSE(config.refresh()) << "nothing outside touched it";
    EXPECT_FALSE(config.refresh());

    // Somebody plugs the watch in and edits the file. Same app, same session:
    // the glance service is restarted by the carousel, but a config that only
    // ever read once would still be wrong for the rest of a viewing.
    fx.fileSystem.seedFile(kPath, kiraDocument("-33.8688", "151.2093"));
    EXPECT_TRUE(config.refresh());
    EXPECT_EQ(config.status(), HomeConfig::Status::Ok);
    EXPECT_NEAR(config.fix().latDeg, -33.8688, 1e-9);
    EXPECT_NEAR(config.fix().lonDeg, 151.2093, 1e-9);
}

TEST_F(HomeConfigTest, APositionThatGoesAwayTakesTheFixWithIt)
{
    fx.fileSystem.seedFile(kPath, kiraDocument("45.4215", "-75.6972"));
    HomeConfig config(fx.kernel, kPath);
    config.refresh();
    ASSERT_TRUE(config.fix().has());

    fx.fileSystem.seedFile(kPath, "{ \"schema\": 1 }\n");
    EXPECT_TRUE(config.refresh());
    EXPECT_EQ(config.status(), HomeConfig::Status::Rejected);
    EXPECT_FALSE(config.fix().has()) << "the last good position must not survive a bad file";
}

} // namespace
