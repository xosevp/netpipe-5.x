#############################################################################
#
#            Makefile for Ames Lab MP_Lite message passing library
#            Dr Dave Turner - October+ of 1997 - turner@ameslab.gov
#
#############################################################################
SHELL=/bin/sh

# Defaults

LD = ld
LDFLAGS =
NPROC=8
DEFS=

OBJS = mpcore.o f77_wrappers.o timer.o memory.o io.o utils.o copy.o lock.o fifo.o remap.o

MPI_OBJS = mpcore.o f77_wrappers.o timer.o timer_f.o memory.o io.o remap.o mpi_c.o mpi_f.o

# if you have no f77 compiler, switch the # on the next two lines
OBJECTS = $(OBJS) timer_f.o
#OBJECTS = $(OBJS)

# You can choose to use an optimized memory copy routine for x86 machines.
# The default is to use a normal memcpy().
#
# DEFS=-DAMDCOPY           This is recommend for x86 systems.
#                          You also need to add mmx_copy.o to OBJECTS below
#
# DEFS=-DNTCOPY            non-temporal memcopy for main memory copies
# DEFS=-DWACOPY            write allocate copy
# DEFS=-DCBCOPY            write allocate for short, non-temporal for long
# DEFS=-DKMEMCPY           based on the Linux kernel mmx_copy
# DEFS=-DKNTCOPY           based on the Linux kernel fast_copy_page
#
# You can also choose an optimized locking function for x86 or PowerPC
#
# DEFS=-DX86
# DEFS=-DPOWERPC

#DEFS=-DAMDCOPY
#OBJECTS = $(OBJS) timer_f.o mmx_copy.o

PTHREAD_OBJECTS = pthread.o join.o manager.o specific.o wrapsyscall.o cancel.o condvar.o



# Infiniband (mellanox)
IB_INC = $(MTHOME)/include


# The compilers and flags

include make.h
# make.h defaults listed below.  These are altered by make at the base level.
#DEFS=...
#CC = cc
#F77 = f77
#FLAGS=-O2
#all: scalar


# General rules for making mplite

libmplite.a: tcp.o shm.o mpi_wrappers.o $(OBJECTS) bin/shm_cleanup make.h
	@ echo "[1m ar -cr libmplite.a [0m tcp.o shm.o mpi_wrappers.o $(OBJECTS)"
	@ ar -cr libmplite.a tcp.o shm.o mpi_wrappers.o $(OBJECTS)
	@ echo "[1m ranlib libmplite.a [0m"
	@ ranlib libmplite.a
#	ar -ts libmplite.a > /dev/null

ib:
	make "DEFS=$(DEFS) -DINFINIBAND" do_ib

do_ib: ib.o io.o mpcore.o memory.o utils.o timer.o mpi_wrappers.o
	ar -cr libmplite.a ib.o io.o mpcore.o memory.o utils.o timer.o \
	   mpi_wrappers.o
	ranlib libmplite.a

ib.o:	ib.c
	$(CC) -c $(FLAGS) $(DEFS) ib.c $(LIBS) -I $(IB_INC)

.c.o: make.h
	@ echo "[1m $(CC) -c $*.c [0m $(DEFS) $(FLAGS) $(LIBS)"
	@ $(CC) -c $(FLAGS) $(DEFS) $*.c $(LIBS)

.f.o: make.h
	@ echo "[1m $(F77) -c $*.f [0m $(FLAGS)"
	@ $(F77) -c $(FLAGS) $*.f


# Specific options for the internal controller

tcp sig sigio async:
	@ rm -fr .make.h.tmp
	@ head -4 make.h > .make.h.tmp
	@ echo "all:tcp" >> .make.h.tmp
	@ mv .make.h.tmp make.h
	make "DEFS=$(DEFS) -DTCP" do_tcp

  do_tcp: $(OBJECTS) tcp.o shm.o mpi_wrappers.o bin/shm_cleanup
	@ echo "[1m ar -cr libmplite.a [0m"
	@ echo "             tcp.o shm.o mpi_wrappers.o $(OBJECTS)"
	@ ar -cr libmplite.a tcp.o shm.o mpi_wrappers.o $(OBJECTS)
	@ echo "[1m ranlib libmplite.a [0m"
	@ ranlib libmplite.a
	@ ar -ts libmplite.a > /dev/null


sync tcp_sync:
	make "DEFS=$(DEFS) -DNO_SIGNALS" tcp

mvia m-via M-VIA:
	make clean
	@ echo
	make "FLAGS=$(FLAGS) -DVIA_FILE -D_REENTRANT" do_via
	@ echo
	@ echo "You must link your application using -lvipl and -lpthreads"
	@ echo

giganet gn Giganet:
	make clean
	@ echo
	make "FLAGS=$(FLAGS) -DVIA_FILE -D_REENTRANT -DGIGANET" do_via
	@ echo
	@ echo "You must link your application using -lgnivipl and -lpthreads"
	@ echo

do_via: $(OBJECTS) shm.o mpi_wrappers.o via.o
	ar -cr libmplite.a via.o shm.o mpi_wrappers.o $(OBJECTS)
	ranlib libmplite.a


pthreads pthread thread threads:
	@ echo "The pThreads version is not ready for prime time yet!"
	ar -x /usr/lib/libpthread.a $(PTHREAD_OBJECTS)
	make "DEFS=$(DEFS) -DUSE_PTHREADS" "OBJECTS=$(OBJECTS) $(PTHREAD_OBJECTS)" tcp
	rm $(PTHREAD_OBJECTS)

t3e T3E:
	@ echo "        Making MP_Lite for SHMEM on the Cray T3E"
	make "FLAGS=-O2" "F77=f90 -F -dp" "DEFS=$(DEFS)" do_shmem

shmem:
	@ echo "        Making MP_Lite for SHMEM on SGI systems"
	make "FLAGS=-O2" "F77=f90" "DEFS=$(DEFS)" do_shmem

 do_shmem: shmem.o $(OBJECTS) mpi_wrappers.o
	ar -cr libmplite.a shmem.o $(OBJECTS) mpi_wrappers.o
	ranlib libmplite.a

scalar: scalar.o $(OBJECTS) mpi_wrappers.o
	@ echo "[1m ar -cr libmplite.a [0m scalar.o $(OBJECTS) mpi_wrappers.o"
	@ ar -cr libmplite.a scalar.o $(OBJECTS) mpi_wrappers.o
	@ echo "[1m ranlib libmplite.a [0m"
	@ ranlib libmplite.a

bin/shm_cleanup: shm_cleanup.o
	$(CC) -o bin/shm_cleanup $(FLAGS) shm_cleanup.o

trace:
	make "DEFS=-DTRACE"

debug1:
	rm -f timer.o mpcore.o io.o
	make "DEFS=-DDEBUG1"

debug2:
	rm -f timer.o mpcore.o io.o
	make "DEFS=-DDEBUG2" "FLAGS=-g"

debug3:
	rm -f timer.o mpcore.o io.o
	make "DEFS=-DDEBUG3" "FLAGS=-g"

mpi: $(MPI_OBJS)
	@ echo "[1m ar -cr libmplite.a $(MPI_OBJS)[0m"
	@ ar -cr libmplite.a $(MPI_OBJS)
	ranlib libmplite.a

#@ echo "[1m mpicc -c mpi_c.c [0m $(DEFS) $(FLAGS)"
#@ mpicc  $(FLAGS) $(DEFS) -c mpi_c.c
#@ echo "[1m mpif77 -c mpi_f.f [0m $(DEFS) $(FLAGS)"
#@ mpif77 $(FLAGS) $(DEFS) -c mpi_f.f
#make "DEFS=$(DEFS) -DMPI" do_mpi
#do_mpi: $(MPI_OBJS)

sp sp_mpi:
	make "DEFS=$(DEFS) -DMPI" do_sp

 do_sp: $(MPI_OBJS)
	mpcc   $(FLAGS) $(DEFS) -c mpi_c.c
	mpxlf  $(FLAGS) -c mpi_f.f
	ld  -r -o mpi.o mpi_c.o mpi_f.o -L/usr/lpp/ppe.poe/lib/ -lmpi
	ar -cr libmplite.a mpi.o $(MPI_OBJS)
	ranlib libmplite.a


# Cray doesn't allow use of ld, and doesn't need -lmpi specified

mpi_cray mpi_t3e t3e_mpi cray_mpi:
	make "DEFS=$(DEFS) -DMPI" "FLAGS= " "F77=$(F77)" do_t3e_mpi

 do_t3e_mpi: mpicc.o mpicf.o $(OBJECTS)
	ar -cr libmplite.a mpicc.o mpicf.o $(OBJECTS)
	ranlib libmplite.a

mpicc.o: mpi_c.c
	$(CC) $(FLAGS) -c mpi_c.c
	mv mpi_c.o mpicc.o

mpicf.o: mpi_f.f
	$(F77) $(FLAGS) -c mpi_f.f
	mv mpi_f.o mpicf.o

# Set the compilers for specific systems, then make using the default setup

gnu:
	@ rm -fr .make.h.tmp
	@ echo "CC = gcc" > .make.h.tmp
	@ echo "F77 = gfortran" >> .make.h.tmp
	@ echo "FLAGS = -O6" >> .make.h.tmp
	@ echo "LDFLAGS = " >> .make.h.tmp
	@ tail -1 make.h >> .make.h.tmp
	@ mv .make.h.tmp make.h

intel icc ifort:
	@ rm -fr .make.h.tmp
	@ echo "CC = icc" > .make.h.tmp
	@ echo "F77 = ifort" >> .make.h.tmp
	@ echo "FLAGS = -O2" >> .make.h.tmp
	@ echo "LDFLAGS = -lirc" >> .make.h.tmp
	@ tail -1 make.h >> .make.h.tmp
	@ mv .make.h.tmp make.h

mic phi:
	@ rm -fr .make.h.tmp
	@ echo "CC = icc" > .make.h.tmp
	@ echo "F77 = ifort" >> .make.h.tmp
	@ echo "FLAGS = -O2 -mmic" >> .make.h.tmp
	@ echo "LDFLAGS = -mmic" >> .make.h.tmp
	@ tail -1 make.h >> .make.h.tmp
	@ mv .make.h.tmp make.h

aix ibm:
	make "CC=xlc" "F77=xlf"

pg pgi:
	@ rm -fr .make.h.tmp
	@ echo "CC = pgcc" > .make.h.tmp
	@ echo "F77 = pgf77" >> .make.h.tmp
	@ echo "FLAGS = -O2" >> .make.h.tmp
	@ echo "LDFLAGS = " >> .make.h.tmp
	@ tail -1 make.h >> .make.h.tmp
	@ mv .make.h.tmp make.h


pong: pong.c libmplite.a
	$(CC) $(FLAGS) -o pong pong.c -L. -lmplite

pong-shmem shmem_pong: pong.c libmplite.a
	$(CC) $(FLAGS) -o pong pong.c -L. -lmplite -lsma

pong-mvia mvia-pong: pong.c libmplite.a
	$(CC) $(FLAGS) -o pong pong.c -L. -lmplite -lvipl -lpthread

pong-gn gn-pong: pong.c libmplite.a
	$(CC) $(FLAGS) -o pong pong.c -L. -lmplite -lgnivipl -lpthread

pong-mpi mpi-pong: pong.c libmplite.a
	mpicc $(FLAGS) -o pong pong.c -L. -lmplite

pong-f f-pong: pong.f libmplite.a
	$(F77) $(FLAGS) -o pong_f pong.f -L. -lmplite

shmtest: shmtest.o libmplite.a
	$(F77) $(FLAGS) -o shmtest shmtest.o -L. -lmplite


clean clena:
	@ echo ""
	@ echo "         Cleaning the MP_Lite directory"
	@ echo ""
	@ echo "rm -f *.o libmplite*.a bin/shm_cleanup pong work.*"
	@ rm -f *.o libmplite*.a bin/shm_cleanup pong work.*

make.h: make.h.def
	cp make.h.def make.h
