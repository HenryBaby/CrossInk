add_executable(BootDisplayPolicyTest
  BootDisplayPolicyTest.cpp
)

target_include_directories(BootDisplayPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BootDisplayPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BootDisplayPolicyTest)
