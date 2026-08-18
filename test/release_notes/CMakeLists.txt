add_test(
  NAME release_notes_generator
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/release_notes_test.py
)
