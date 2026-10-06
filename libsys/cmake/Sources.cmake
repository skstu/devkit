# Platform source lists imported from SovKit; owned here after extraction.

set(LIBSYS_COMMON_SOURCES
  src/libsys.cc
  src/stacktrace.cc
)

set(LIBSYS_windows_SOURCES
  src/windows/device.cc
  src/windows/egress.cc
  src/windows/gpu.cc
  src/windows/net.cc
  src/windows/os.cc
  src/windows/utils.cc
)

set(LIBSYS_windows_LIBRARIES
  dbghelp
  dxgi
  setupapi
  iphlpapi
  winhttp
  dnsapi
  ws2_32
  shell32
  ole32
)

set(LIBSYS_macos_SOURCES
  src/apple/activation.mm
  src/apple/device.cc
  src/apple/egress.mm
  src/apple/gpu.mm
  src/apple/net.cc
  src/apple/os.cc
  src/apple/utils.cc
)

set(LIBSYS_macos_LIBRARIES
)

set(LIBSYS_ios_SOURCES
  src/mobile/mobile.cc
)

set(LIBSYS_ios_LIBRARIES
)

set(LIBSYS_android_SOURCES
  src/mobile/mobile.cc
)

set(LIBSYS_android_LIBRARIES
  dl
)

set(LIBSYS_linux_SOURCES
  src/linux/device.cc
  src/linux/egress.cc
  src/linux/gpu.cc
  src/linux/net.cc
  src/linux/os.cc
  src/linux/utils.cc
)

set(LIBSYS_linux_LIBRARIES
)
