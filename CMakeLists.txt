cmake_minimum_required(VERSION 3.0)
set (CMAKE_INSTALL_PREFIX /usr)

# require Pkg-Config
FIND_PACKAGE(PkgConfig REQUIRED)

# Find systemd service directory
pkg_check_modules(SYSTEMD "systemd")
if (SYSTEMD_FOUND AND "${SYSTEMD_SERVICES_INSTALL_DIR}" STREQUAL "")
	execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
		--variable=systemdsystemunitdir systemd
		OUTPUT_VARIABLE SYSTEMD_SERVICES_INSTALL_DIR)
	string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_SERVICES_INSTALL_DIR
		"${SYSTEMD_SERVICES_INSTALL_DIR}")
elseif (NOT SYSTEMD_FOUND AND SYSTEMD_SERVICES_INSTALL_DIR)
	message (FATAL_ERROR "Variable SYSTEMD_SERVICES_INSTALL_DIR is\
		defined, but we can't find systemd using pkg-config")
endif()

if (SYSTEMD_FOUND)
	set(WITH_SYSTEMD "ON")
	message(STATUS "systemd services install dir: ${SYSTEMD_SERVICES_INSTALL_DIR}")
else()
	set(WITH_SYSTEMD "OFF")
endif (SYSTEMD_FOUND)

project (dnscrypt-proxy-gui)
set (APP_NAME DNSCryptClient)

include(CheckFunctionExists)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if (COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

add_subdirectory(src)

install ( FILES       ${APP_NAME}.desktop
          DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications )
install ( FILES       ${APP_NAME}@.service
          DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR} )
install ( FILES       ${APP_NAME}_test@.service
          DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR} )
install ( FILES       ${APP_NAME}_test_v2.service
          DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR} )
install ( FILES src/icons/64x64/status/${APP_NAME}.png
          DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/64x64/apps
          RENAME ${APP_NAME}.png )
