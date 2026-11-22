# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(zlib DLL RETAIL DEF zlib.def
  SOURCES
    vendor/adler32.c
    vendor/compress.c
    vendor/crc32.c
    vendor/deflate.c
    vendor/gzclose.c
    vendor/gzlib.c
    vendor/gzread.c
    vendor/gzwrite.c
    vendor/inffast.c
    vendor/inflate.c
    vendor/inftrees.c
    vendor/trees.c
    vendor/uncompr.c
    vendor/zutil.c
    vendor/minizip/ioapi.c
    vendor/minizip/unzip.c
    vendor/minizip/zip.c
    zlib_version_compat.c
    zlib.rc
)

fl_includes(zlib vendor vendor/minizip)
fl_defines(zlib ZLIB_WINAPI _CRT_NONSTDC_NO_DEPRECATE)
fl_cl_options(zlib /wd4244 /wd4267 /wd4996)
fl_link(zlib)
