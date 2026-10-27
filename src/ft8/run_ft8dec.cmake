# Run the MinGW FT8 decoder build. Ninja wraps custom commands in cmd /C,
# which strips quotes from a path that contains spaces, so the .cmd is
# started from this script instead.
if(NOT SCRIPT OR NOT OUT)
  message(FATAL_ERROR "SCRIPT and OUT are required")
endif()
execute_process(
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/invoke_ft8dec.ps1" -Script "${SCRIPT}" -Out "${OUT}"
  RESULT_VARIABLE rc
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "FT8 decoder build failed (${rc})")
endif()
