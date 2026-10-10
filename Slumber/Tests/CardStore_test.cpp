#include <gtest/gtest.h>

#include <set>
#include <string>

#include "CardStore.hpp"
#include "FakeFileSystem.hpp"

using namespace SlumberCard;

namespace
{

/// A file whose open() fails, as one held open for writing by another app does.
class LockedFile : public SDK::Test::FakeFile
{
public:
    using FakeFile::FakeFile;
    bool open(bool, bool) override { return false; }
};

/// The SDK's fake lets rename() replace an existing file; the watch does not
/// (una-sdk PR #270), and the store's write order exists because of that.
class WatchFs : public SDK::Test::FakeFileSystem
{
public:
    std::set<std::string> locked;

    bool rename(const char *oldPath, const char *newPath) override
    {
        if (exist(newPath)) {
            return false;
        }
        return FakeFileSystem::rename(oldPath, newPath);
    }

    std::unique_ptr<SDK::Interface::IFile> file(const char *path) override
    {
        if (locked.count(path) != 0) {
            return std::unique_ptr<SDK::Interface::IFile>(new LockedFile(*this, path));
        }
        return FakeFileSystem::file(path);
    }
};

Card withSeq(uint32_t seq)
{
    Card c;
    c.seq   = seq;
    c.state = State::Probe;
    return c;
}

std::string encoded(const Card &c)
{
    uint8_t bytes[kSize];
    encode(c, bytes);
    return std::string(reinterpret_cast<const char *>(bytes), kSize);
}

} // namespace

TEST(CardStore, NothingPublishedIsAbsent)
{
    WatchFs fs;
    Card out;
    EXPECT_EQ(load(fs, out), Read::Absent);
}

TEST(CardStore, FirstPublishIsReadBack)
{
    WatchFs fs;
    ASSERT_TRUE(publish(fs, withSeq(1)));
    EXPECT_TRUE(fs.dirCreated(kDir));
    Card out;
    ASSERT_EQ(load(fs, out), Read::Ok);
    EXPECT_EQ(out.seq, 1u);
}

/// Under the watch's rename rule, the second publish is the one a naive
/// write-temp-then-rename gets wrong.
TEST(CardStore, RepublishReplacesAndKeepsThePreviousAsBackup)
{
    WatchFs fs;
    for (uint32_t seq = 1; seq <= 4; ++seq) {
        ASSERT_TRUE(publish(fs, withSeq(seq))) << seq;
    }
    Card out;
    ASSERT_EQ(load(fs, out), Read::Ok);
    EXPECT_EQ(out.seq, 4u);
    EXPECT_EQ(fs.fileContents(kBakPath), encoded(withSeq(3)));
    EXPECT_FALSE(fs.hasFile(kTmpPath));
}

TEST(CardStore, ATornPrimaryFallsBackToTheBackup)
{
    WatchFs fs;
    fs.seedFile(kPath, encoded(withSeq(5)).substr(0, 20));
    fs.seedFile(kBakPath, encoded(withSeq(4)));
    Card out;
    ASSERT_EQ(load(fs, out), Read::FromBackup);
    EXPECT_EQ(out.seq, 4u);
}

/// A crash between rotating the primary away and promoting the temp.
TEST(CardStore, ACrashMidRotateStillLeavesAGoodCopy)
{
    WatchFs fs;
    fs.seedFile(kBakPath, encoded(withSeq(4)));
    fs.seedFile(kTmpPath, encoded(withSeq(5)));
    Card out;
    ASSERT_EQ(load(fs, out), Read::FromBackup);
    EXPECT_EQ(out.seq, 4u);

    ASSERT_TRUE(publish(fs, withSeq(6)));
    ASSERT_EQ(load(fs, out), Read::Ok);
    EXPECT_EQ(out.seq, 6u);
}

/// The PR #270 caveat: a reader must not take "being written" for "never written".
TEST(CardStore, ALockedFileIsBusyNotAbsent)
{
    WatchFs fs;
    ASSERT_TRUE(publish(fs, withSeq(1)));
    fs.locked = { kPath, kBakPath };
    Card out;
    EXPECT_EQ(load(fs, out), Read::Busy);
}

TEST(CardStore, OnlyAnOrphanedTempIsBusyNotAbsent)
{
    WatchFs fs;
    fs.seedFile(kTmpPath, encoded(withSeq(1)).substr(0, 10));
    Card out;
    EXPECT_EQ(load(fs, out), Read::Busy);
}
