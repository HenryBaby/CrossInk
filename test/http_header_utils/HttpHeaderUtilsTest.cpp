#include <gtest/gtest.h>

#include "HttpHeaderUtils.h"

TEST(HttpHeaderUtilsTest, ExtractsQuotedAsciiFilename) {
  std::string filename;
  EXPECT_TRUE(
      HttpHeaderUtils::extractContentDispositionFilename("attachment; filename=\"Series 01 - Book.epub\"", filename));
  EXPECT_EQ(filename, "Series 01 - Book.epub");
}

TEST(HttpHeaderUtilsTest, PrefersUtf8ExtendedFilename) {
  std::string filename;
  EXPECT_TRUE(HttpHeaderUtils::extractContentDispositionFilename(
      "attachment; filename=\"fallback.epub\"; filename*=UTF-8''M%C3%B6rkret%2001.epub", filename));
  EXPECT_EQ(filename, "M\xC3\xB6rkret 01.epub");
}

TEST(HttpHeaderUtilsTest, HandlesQuotedSemicolonAndEscape) {
  std::string filename;
  EXPECT_TRUE(HttpHeaderUtils::extractContentDispositionFilename("attachment; filename=\"Series; \\\"Book\\\".epub\"",
                                                                 filename));
  EXPECT_EQ(filename, "Series; \"Book\".epub");
}

TEST(HttpHeaderUtilsTest, FallsBackWhenExtendedFilenameIsMalformed) {
  std::string filename;
  EXPECT_TRUE(HttpHeaderUtils::extractContentDispositionFilename(
      "attachment; filename=\"fallback.epub\"; filename*=UTF-8''bad%XXname.epub", filename));
  EXPECT_EQ(filename, "fallback.epub");
}

TEST(HttpHeaderUtilsTest, RejectsMissingFilename) {
  std::string filename = "stale";
  EXPECT_FALSE(HttpHeaderUtils::extractContentDispositionFilename("attachment", filename));
  EXPECT_TRUE(filename.empty());
}

TEST(HttpHeaderUtilsTest, RejectsHeaderInjection) {
  std::string filename;
  EXPECT_FALSE(
      HttpHeaderUtils::extractContentDispositionFilename("attachment; filename*=UTF-8''book%0Aevil.epub", filename));
}

TEST(HttpHeaderUtilsTest, RejectsUnterminatedQuotedFilename) {
  std::string filename;
  EXPECT_FALSE(HttpHeaderUtils::extractContentDispositionFilename("attachment; filename=\"book.epub", filename));
}
