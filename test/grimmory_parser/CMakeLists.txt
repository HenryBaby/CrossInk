add_executable(GrimmoryParserTest
  GrimmoryParserTest.cpp
  ${REPO_ROOT}/lib/Grimmory/GrimmoryParser.cpp
)
target_include_directories(GrimmoryParserTest PRIVATE ${REPO_ROOT}/lib/Grimmory)
target_link_libraries(GrimmoryParserTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GrimmoryParserTest)
