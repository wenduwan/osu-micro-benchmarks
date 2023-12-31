AUTOMAKE_OPTIONS = subdir-objects

NVCC = nvcc
NVCFLAGS = -cuda -maxrregcount 32 -ccbin $(CXX) $(NVCCFLAGS)
SUFFIXES = .cu .cpp
.cu.cpp:
	$(NVCC) $(NVCFLAGS) $(INCLUDES) $(CPPFLAGS) --output-file $@.ii $<
	mv $@.ii $@

ncclpt2ptdir = $(pkglibexecdir)/nccl/pt2pt
ncclpt2pt_PROGRAMS = osu_nccl_bibw osu_nccl_bw osu_nccl_latency

AM_CFLAGS = -I${top_srcdir}/c/util

UTILITIES = ../../util/osu_util.c ../../util/osu_util.h \
    ../../util/osu_util_mpi.c ../../util/osu_util_mpi.h \
    ../../util/osu_util_nccl.c ../../util/osu_util_nccl.h \
    ../../util/osu_util_papi.c ../../util/osu_util_papi.h

if CUDA_KERNELS
UTILITIES += ../../util/kernel.cu
CLEANFILES = ../../util/kernel.cpp
if BUILD_USE_PGI
AM_CXXFLAGS = --nvcchost --no_preincludes
endif
endif

osu_nccl_bw_SOURCES = osu_nccl_bw.c $(UTILITIES)
osu_nccl_bibw_SOURCES = osu_nccl_bibw.c $(UTILITIES)
osu_nccl_latency_SOURCES = osu_nccl_latency.c $(UTILITIES)

if EMBEDDED_BUILD
    AM_LDFLAGS = -I$(top_builddir)/../modules/nccl/build/lib -lnccl
    AM_CPPFLAGS = -I$(top_builddir)/../src/include \
		  -I${top_srcdir}/util \
		  -I${top_srcdir}/../src/include \
		  -I${top_srcdir}/../modules/nccl/build/include
if BUILD_PROFILING_LIB
    AM_LDFLAGS += $(top_builddir)/../lib/lib@PMPILIBNAME@.la
endif
    AM_LDFLAGS += $(top_builddir)/../lib/lib@MPILIBNAME@.la
endif

if OPENACC
    AM_CFLAGS += -acc -ta=tesla:nordc
    AM_CXXFLAGS = -acc -ta=tesla:nordc
endif
