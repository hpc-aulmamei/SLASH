add_executable(slash_mock_tests
  slash_mock_tests.c
)

set_target_properties(slash_mock_tests PROPERTIES
  C_STANDARD 90
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
)

target_link_libraries(slash_mock_tests PRIVATE slash)
target_include_directories(slash_mock_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_test(NAME slash_mock_tests COMMAND slash_mock_tests)
