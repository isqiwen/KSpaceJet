#include "kspacejet/crash/ring_buffer.hpp"

#include <array>
#include <cstring>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetCrashRingBuffer, PublishesEntriesInSequenceOrder) {
  ksj::crash::RingBuffer buffer;

  buffer.push(7, "worker", "recon", "first", 100);
  buffer.push(8, "io", "socket", "second", 200);

  std::array<ksj::crash::RingBuffer::Entry, 4> entries{};
  const std::size_t count = buffer.snapshot_recent(entries.data(), entries.size());

  ASSERT_EQ(2U, count);
  EXPECT_EQ(1U, entries[0].sequence);
  EXPECT_EQ(7U, entries[0].thread_id);
  EXPECT_STREQ("worker", entries[0].thread_name.data());
  EXPECT_STREQ("recon", entries[0].category.data());
  EXPECT_STREQ("first", entries[0].message.data());
  EXPECT_EQ(200U, entries[1].monotonic_ms);
  EXPECT_STREQ("second", entries[1].message.data());
}

TEST(KSpaceJetCrashRingBuffer, SuppliesDefaultsForEmptyTextFields) {
  ksj::crash::RingBuffer buffer;

  buffer.push(1, "", "", "message", 5);

  ksj::crash::RingBuffer::Entry entry{};
  ASSERT_EQ(1U, buffer.snapshot_recent(&entry, 1));
  EXPECT_STREQ("unknown-thread", entry.thread_name.data());
  EXPECT_STREQ("general", entry.category.data());
}

TEST(KSpaceJetCrashRingBuffer, ClearRemovesPublishedEntries) {
  ksj::crash::RingBuffer buffer;
  buffer.push(1, "thread", "category", "message", 5);

  buffer.clear();

  ksj::crash::RingBuffer::Entry entry{};
  EXPECT_EQ(0U, buffer.published_count());
  EXPECT_EQ(0U, buffer.snapshot_recent(&entry, 1));
}

} // namespace
