#include <gtest/gtest.h>

#include "GrimmoryParser.h"

TEST(GrimmoryParser, SanitizesPathAndExtension) {
  EXPECT_TRUE(Grimmory::sanitizeEpubFilename("../A/B:book.PDF").empty());
  EXPECT_EQ(Grimmory::sanitizeEpubFilename("A/B:book.PDF"), "A_B_book.epub");
  EXPECT_TRUE(Grimmory::sanitizeEpubFilename("..").empty());
  EXPECT_TRUE(Grimmory::sanitizeEpubFilename(".epub").empty());
}

TEST(GrimmoryParser, ParsesEpubEntriesAndTitleFallback) {
  const std::string json = R"({"content":[
    {"id":1,"metadata":{"title":"First","authors":["A","B"]},"primaryFile":{"fileName":"first.epub"}},
    {"id":2,"metadata":{"title":"Second"},"primaryFile":{"fileName":"second.pdf"}},
    {"id":3,"metadata":{"title":"Third"},"primaryFile":null}],"page":{"totalElements":3,"number":0}})";
  std::vector<Grimmory::BookEntry> entries;
  size_t total = 0;
  ASSERT_TRUE(Grimmory::parsePageResponse(json, entries, total));
  ASSERT_EQ(total, 3u);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].filename, "first.epub");
  EXPECT_EQ(entries[0].author, "A");
  EXPECT_EQ(entries[1].filename, "Third.epub");
}

TEST(GrimmoryParser, ParsesCompactAppBooksAndRejectsNonEpub) {
  const std::string json = R"({"content":[{"id":7,"title":"Compact","authors":["Ada"],"primaryFileName":"compact.epub","primaryFileType":"EPUB"},{"id":8,"title":"PDF","authors":["Bob"],"primaryFileName":"x.pdf","primaryFileType":"PDF"}],"totalElements":2})";
  std::vector<Grimmory::BookEntry> entries;
  size_t total = 0;
  ASSERT_TRUE(Grimmory::parsePageResponse(json, entries, total));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].id, 7);
  EXPECT_EQ(entries[0].author, "Ada");
  EXPECT_EQ(total, 2u);
}

TEST(GrimmoryParser, BoundsEntries) {
  std::string json = R"({"content":[)";
  for (size_t i = 0; i < 25; ++i) {
    if (i) json += ',';
    json += "{\"id\":" + std::to_string(i) +
            ",\"metadata\":{\"title\":\"Book\"},\"primaryFile\":{\"fileName\":\"book.epub\"}}";
  }
  json += R"]],"totalElements":25})";
  std::vector<Grimmory::BookEntry> entries;
  size_t total = 0;
  ASSERT_TRUE(Grimmory::parsePageResponse(json, entries, total));
  EXPECT_EQ(entries.size(), Grimmory::kMaxEntries);
}
