add_executable(HttpHeaderUtilsTest
  HttpHeaderUtilsTest.cpp
  ${REPO_ROOT}/lib/HttpHeaderUtils/HttpHeaderUtils.cpp
)

target_include_directories(HttpHeaderUtilsTest PRIVATE
  ${REPO_ROOT}/lib/HttpHeaderUtils
)

target_link_libraries(HttpHeaderUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpHeaderUtilsTest)
