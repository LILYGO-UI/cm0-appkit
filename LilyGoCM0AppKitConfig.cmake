if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/CM0AppKit.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/CM0AppKit.cmake")
else()
    include("${CMAKE_CURRENT_LIST_DIR}/../LilyGoUI/LilyGoUIConfig.cmake")
endif()

set(LilyGoCM0AppKit_FOUND TRUE)
