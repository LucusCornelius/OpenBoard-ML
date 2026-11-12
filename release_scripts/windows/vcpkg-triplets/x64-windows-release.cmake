# x64-windows, but release libraries only: DLLs linked against the dynamic CRT (/MD), like the
# official Qt binaries and OpenBoard.pro. Skipping the debug variants halves the vcpkg build time.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
