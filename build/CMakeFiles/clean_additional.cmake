# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/CanvasPDF_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/CanvasPDF_autogen.dir/ParseCache.txt"
  "CanvasPDF_autogen"
  )
endif()
