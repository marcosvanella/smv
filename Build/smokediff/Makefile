
# smokediff makefile

SOURCE_DIR = ../../../Source
INC_DIR = -I $(SOURCE_DIR)/zlib128 -I $(SOURCE_DIR)/shared -I $(SOURCE_DIR)/smokediff
FILTERC =
FILTERF =

SMV_TESTFLAG=

VPATH = $(SOURCE_DIR)/smokediff ../../../../FDS/Source $(SOURCE_DIR)/shared $(SOURCE_DIR)/smokeview
bin = .

ifeq ($(shell echo "check_quotes"),"check_quotes")
  GIT_HASH := $(shell ..\..\..\Utilities\Scripts\githash)
  GIT_DATE := $(shell ..\..\..\Utilities\Scripts\gitlog)
else
  GIT_HASH := $(shell git describe --long --dirty)
  GIT_DATE := $(shell git log -1 --format=%cd)
endif
GITINFO=-Dpp_GITHASH=\"$(GIT_HASH)\" -Dpp_GITDATE=\""$(GIT_DATE)\""

ifeq ($(shell echo "check_quotes"),"check_quotes")
  INTEL_COMPVERSION := $(shell ..\..\..\Utilities\Scripts\intel_compversion)
  GNU_COMPVERSION := $(shell ..\..\..\Utilities\Scripts\gnu_compversion)
else
  INTEL_COMPVERSION := $(shell ../../../Utilities/Scripts/intel_compversion.sh)
  GNU_COMPVERSION := $(shell ../../../Utilities/Scripts/gnu_compversion.sh)
endif
INTEL_COMPINFO=-Dpp_COMPVER=\"$(INTEL_COMPVERSION)\"
GNU_COMPINFO=-Dpp_COMPVER=\"$(GNU_COMPVERSION)\"

FORTLIBDIR = /opt/intel/composerxe/lib

# Definition of the object variables

obj = assert.o dmalloc.o histogram.o IOdboundary.o IOdplot.o IOdslice.o main.o readsmv.o\
      file_util.o string_util.o utilities.o  md5.o sha1.o sha256.o stdio_buffer.o\
      fdsmodules.o gsmv.o getdata.o
objwin = $(obj:.o=.obj)

#*** General Purpose Rules ***

no_target:
	@echo \******** You did not specify a make target \********
	@echo Use one of targets found in the Makefile

.SUFFIXES: .F90 .c .obj .o .f90 .cpp

.c.obj:
	$(CC) -c $(CFLAGS) $(FILTERC) $(INC_DIR) $<
.c.o:
	$(CC) -c $(CFLAGS) $(FILTERC) $(INC_DIR) $<
.F90.o:
	$(FC) -c $(FFLAGS) $(FILTERF) $<
.F90.obj:
	$(FC) -c $(FFLAGS) $(FILTERF) $<
.f90.obj:
	$(FC) -c $(FFLAGS) $(FILTERF) $<
.f90.o:
	$(FC) -c $(FFLAGS) $(FILTERF) $<

# ------------- intel winx 64 db ----------------

intel_win_64_db : FFLAGS    = /Od /iface:stdref /fpp -D WIN32 /nologo /debug:full /extend_source:132 /warn:unused /warn:nointerfaces /Qtrapuv /fp:strict /fp:except /traceback /check:all /stand:f18 /fpscomp:general
intel_win_64_db : CFLAGS    = /Od /Wall /debug:full /W4 /ZI -D WIN32 -D _CONSOLE -D X64 -D GLEW_STATIC -D PTW32_STATIC_LIB $(SMV_TESTFLAG) $(GITINFO) $(INTEL_COMPINFO)
intel_win_64_db : CC        = icl
intel_win_64_db : CPP       = icl
intel_win_64_db : FC        = ifort
intel_win_64_db : exe       = smokediff_win_64_db.exe

intel_win_64_db : $(objwin)
	$(CPP) -o $(bin)/$(exe) $(objwin)

# ------------- intel win 64 ----------------

intel_win_64 : FFLAGS    = /O2 -fpp -D WIN32 /nologo /iface:stdref /stand:f18 /fpscomp:general
intel_win_64 : CFLAGS    = /O2 /nologo -D X64 -D WIN32 $(GITINFO) $(SMV_TESTFLAG) $(INTEL_COMPINFO)
intel_win_64 : CC        = icl
intel_win_64 : CPP       = icl
intel_win_64 : FC        = ifort
intel_win_64 : exe       = smokediff_win_64.exe

intel_win_64 : $(objwin)
	$(CPP) -o $(bin)/$(exe) $(objwin)

# ------------- gnu win 64 ----------------

gnu_win_64 : FFLAGS    = -O0 -m64 -x f95-cpp-input -D pp_GCC -ffree-form -frecord-marker=4
gnu_win_64 : CFLAGS    = -O0 -m64 -D pp_LINUX -D GLEW_STATIC -D MINGW $(SMV_TESTFLAG)
gnu_win_64 : LFLAGS    = -m64
gnu_win_64 : CC        = gcc
gnu_win_64 : CPP       = g++
gnu_win_64 : FC        = gfortran
gnu_win_64 : exe       = smokediff_win_64.exe

gnu_win_64 : $(obj) 
	$(FC) -o $(bin)/$(exe) $(LFLAGS) $(obj)

LINUXFORTLIBS=$(IFORT_COMPILER_LIB)/libifcore.a

# ------------- gnu linux 64 ----------------

gnu_linux_64 : FFLAGS    = -O0 -m64 -x f95-cpp-input -D pp_GCC -D pp_LINUX -ffree-form -frecord-marker=4 $(GNU_COMPINFO)
gnu_linux_64 : CFLAGS    = -O0 -m64 -D pp_GCC -D pp_LINUX $(GITINFO) $(GNU_COMPINFO) $(SMV_TESTFLAG)
gnu_linux_64 : LFLAGS    = -m64
gnu_linux_64 : CC        = gcc
gnu_linux_64 : CPP       = g++
gnu_linux_64 : FC        = gfortran
gnu_linux_64 : exe       = smokediff_linux_64

gnu_linux_64 : $(obj)
	$(CPP) -o $(bin)/$(exe) $(obj) $(LFLAGS) -lgfortran

# ------------- gnu linux 64 db ----------------

gnu_linux_64_db : FFLAGS    = -O0 -m64 -ggdb -Wall -x f95-cpp-input -D pp_GCC -ffree-form -frecord-marker=4 -fcheck=all -fbacktrace
gnu_linux_64_db : CFLAGS    = -O0 -m64 -ggdb -Wall -Wno-parentheses -Wno-unknown-pragmas -Wno-comment -Wno-write-strings -D _DEBUG -D pp_LINUX -D pp_GCC $(GITINFO) $(GNU_COMPINFO) $(SMV_TESTFLAG)
gnu_linux_64_db : LFLAGS    = -m64
gnu_linux_64_db : CC        = gcc
gnu_linux_64_db : CPP       = g++
gnu_linux_64_db : FC        = gfortran
gnu_linux_64_db : exe       = smokediff_linux_64_db

gnu_linux_64_db : $(obj)
	$(CPP) -o $(bin)/$(exe) $(obj) $(LFLAGS) -lgfortran


# ------------- gnu osx 64 ----------------

gnu_osx_64 : FFLAGS    = -O0 -m64 -x f95-cpp-input -D pp_GCC -D pp_OSX -ffree-form -frecord-marker=4 $(GNU_COMPINFO)
gnu_osx_64 : CFLAGS    = -O0 -m64 -D pp_GCC -D  pp_OSX $(GITINFO) $(GNU_COMPINFO) $(SMV_TESTFLAG)
gnu_osx_64 : LFLAGS    = -m64
gnu_osx_64 : CC        = gcc
gnu_osx_64 : CPP       = g++
gnu_osx_64 : FC        = gfortran
gnu_osx_64 : exe       = smokediff_osx_64

gnu_osx_64 : $(obj)
	$(CPP) -o $(bin)/$(exe) $(obj) $(LFLAGS) -L $(GLIBDIR) -lgfortran

# ------------- gnu osx 64 db ----------------

gnu_osx_64_db : FFLAGS    = -O0 -m64 -x f95-cpp-input -D pp_GCC -D pp_OSX -ffree-form -frecord-marker=4
gnu_osx_64_db : CFLAGS    = -O0 -m64 -Wall -D _DEBUG -D pp_OSX -D pp_GCC -Wno-deprecated-declarations -Wno-write-strings $(GNU_COMPINFO) $(GITINFO) $(SMV_TESTFLAG)
gnu_osx_64_db : LFLAGS    = -m64
gnu_osx_64_db : CC        = gcc
gnu_osx_64_db : CPP       = g++
gnu_osx_64_db : FC        = gfortran
gnu_osx_64_db : exe       = smokediff_osx_64_db

gnu_osx_64_db : $(obj)
	$(CPP) -o $(bin)/$(exe) $(obj) $(LFLAGS) -L $(GLIBDIR) -lgfortran

# ------------- intel linux 64 ----------------

intel_linux_64 : FFLAGS    = -O2 -m64 -fpp -D pp_LINUX -stand:f18
intel_linux_64 : CFLAGS    = -O2 -m64 -D pp_LINUX $(GITINFO) $(INTEL_COMPINFO) $(SMV_TESTFLAG)
intel_linux_64 : CC        = icc
intel_linux_64 : CPP       = icc
intel_linux_64 : FC        = ifort
intel_linux_64 : exe       = smokediff_linux_64

intel_linux_64 : $(obj)
	$(CPP) -o $(bin)/$(exe) -static-intel $(obj) -lifport $(LINUXFORTLIBS)

# ------------- intel linux 64 db ----------------

intel_linux_64_db : FFLAGS    = -O0 -m64 -fpp -D pp_LINUX -stand:f18
intel_linux_64_db : CFLAGS    = -O0 -m64 -D pp_LINUX -g -check=stack,uninit -fp-stack-check -fp-trap-all=divzero,invalid,overflow -ftrapuv -Wuninitialized -Wunused-function -Wunused-variable $(GITINFO) $(INTEL_COMPINFO) $(SMV_TESTFLAG)
intel_linux_64_db : CC        = icc
intel_linux_64_db : CPP       = icc
intel_linux_64_db : FC        = ifort
intel_linux_64_db : exe       = smokediff_linux_64_db

intel_linux_64_db : $(obj)
	$(CPP) -o $(bin)/$(exe) -static-intel $(obj) -lifport $(LINUXFORTLIBS)
	

MACFORTLIBS=$(IFORT_COMPILER_LIB)/libifcoremt.a $(IFORT_COMPILER_LIB)/libifport.a

# ------------- intel osx 64 ----------------

intel_osx_64 : FFLAGS    = -O2 -m64 -fpp -D pp_OSX -mmacosx-version-min=10.9  -stand:f18 
intel_osx_64 : CFLAGS    = -O2 -m64 -D pp_OSX $(GITINFO) $(INTEL_COMPINFO) -mmacosx-version-min=10.9  $(SMV_TESTFLAG)
intel_osx_64 : CC        = icc
intel_osx_64 : CPP       = icc
intel_osx_64 : FC        = ifort
intel_osx_64 : exe       = smokediff_osx_64

intel_osx_64 : $(obj)
	$(CPP) -o $(bin)/$(exe) -m64 -static-intel $(obj) -mmacosx-version-min=10.9 $(MACFORTLIBS)

# ------------- intel osx 64 db ----------------

intel_osx_64_db : FFLAGS    = -O0 -m64 -static-intel -fpp -D pp_OSX -mmacosx-version-min=10.9 -stand:f18
intel_osx_64_db : CFLAGS    = -O0 -g -m64 -static-intel -D _DEBUG -D pp_OSX -mmacosx-version-min=10.7 $(SMV_TESTFLAG) -Wall -Wextra -check=stack,uninit -fp-stack-check -fp-trap-all=divzero,invalid,overflow -ftrapuv -Wuninitialized -Wunused-function -Wunused-variable $(GITINFO) $(INTEL_COMPINFO)
intel_osx_64_db : CC        = icc
intel_osx_64_db : CPP       = icc
intel_osx_64_db : FC        = ifort
intel_osx_64_db : exe       = smokediff_osx_64_db

intel_osx_64_db : $(obj)
	$(CPP) -o $(bin)/$(exe) -m64 -static-intel $(obj) -mmacosx-version-min=10.9 $(MACFORTLIBS)

#-------------- compile sring_util.c so revision strings are updated -----------

string_util.o:  .FORCE
string_util.obj:        .FORCE

.FORCE:

