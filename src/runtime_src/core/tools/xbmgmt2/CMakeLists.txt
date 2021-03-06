# -----------------------------------------------------------------------------

set(Boost_USE_STATIC_LIBS ON)               # Only find static libraries
set(Boost_USE_MULTITHREADED ON)             # Multi-threaded libraries

find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
include_directories(${Boost_INCLUDE_DIRS})

# -----------------------------------------------------------------------------

include_directories(${CMAKE_BINARY_DIR})

# -----------------------------------------------------------------------------

file(GLOB XBMGMT_V2_BASE_FILES
  "xbmgmt.cpp"
  "../common/XBMain.cpp"
  "../common/XBUtilities.cpp"
  "../common/SubCmd.cpp"
)

if(WIN32)
  set(XBMGMT2_NAME "xbmgmt")     # Yes, on windows the file name will be xbmgmt
  file(GLOB XBMGMT_V2_SUBCMD_FILES
    "SubCmdFlash.cpp"
    "SubCmdVersion.cpp"
    "flash/firmware_image.cpp"
    "flash/xmc.cpp"
    "flash/xspi.cpp"
    "flash/flasher.cpp"
  )
else()
  set(XBMGMT2_NAME "xbmgmt2")
  file(GLOB XBMGMT_V2_SUBCMD_FILES
    "SubCmdFlash.cpp"
    "SubCmdVersion.cpp"
    "flash/firmware_image.cpp"
    "flash/xmc.cpp"
    "flash/xspi.cpp"
    "flash/flasher.cpp"
  )
endif()

set(XBMGMT_V2_FILES_SRCS ${XBMGMT_V2_BASE_FILES} ${XBMGMT_V2_SUBCMD_FILES})
set(XBMGMT_V2_SRCS ${XBMGMT_V2_FILES_SRCS})

add_executable(${XBMGMT2_NAME} ${XBMGMT_V2_SRCS})


if(WIN32)
  target_link_libraries(
    ${XBMGMT2_NAME} PRIVATE
    Boost::filesystem
    Boost::program_options
    Boost::system
    ws2_32
    xrt_core
    xrt_core_static
    xrt_coreutil_static
  )

else()
  target_link_libraries(
    ${XBMGMT2_NAME}
    xrt_core
    xrt_core_static
    xrt_coreutil_static
    boost_filesystem
    boost_system
    boost_program_options
    uuid
    dl
  )
endif()

install (TARGETS ${XBMGMT2_NAME} RUNTIME DESTINATION ${XRT_INSTALL_DIR}/bin)

# -----------------------------------------------------------------------------
