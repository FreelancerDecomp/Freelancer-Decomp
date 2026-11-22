# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(qhull LIB RETAIL PORTABLE WARNING_LEVEL 1
  SOURCES
    vendor/geom.c
    vendor/geom2.c
    vendor/global.c
    vendor/io.c
    vendor/mem.c
    vendor/merge.c
    vendor/poly.c
    vendor/poly2.c
    vendor/qhull.c
    vendor/qset.c
    vendor/stat.c
    vendor/user.c
)

# qhull 2002.1 (vendored under vendor\), the classic non-reentrant global-qh_qh API. Retail
# Common.dll links an older "ipion 99/07/10" qhull (2.6 era): several functions differ from
# 2002.1. The vendored source is modified only where it was aligned with the retail listing: the
# error sink (FUN_06356960 instead of fprintf(qh ferr) at the retail sites, each marked with its
# retail address), qh_new_qhull / qh_errexit / qh_errprint / qh_printlists, qh_VERSION and
# qh_CPUclock. See src/qhull/vendor/COPYING.txt for the Qhull license and required attribution.
fl_includes(qhull vendor)
fl_defines(qhull _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
fl_cl_options(qhull /wd4013 /wd4018 /wd4028 /wd4090 /wd4101 /wd4113 /wd4131 /wd4133 /wd4142 /wd4152 /wd4189 /wd4244 /wd4267 /wd4305 /wd4306 /wd4700 /wd4701 /wd4996)
