########################################################################
# This is the makefile for NetPIPE
# Simply type make with one of the following choices for environments:
#
# The 2-sided modules below use mpicc to compile and MPI to handshake/bcast/gather/sync
#      mpi         : Test MPI between/within nodes for 1 or more pairs
#      ibverbs     : Test ibverbs RC or UD, 2-sided or RDMA, inline
#      tcp         : Test TCP sockets
#
#      memcpy      : measure memcpy(), memset(), dcopy, dread, dwrite
#      disk        : measure read/write IO rates and file cp
#      theo        : create a theoretical performance curve
#
# If you don't have MPI installed you can use a research version MP_Lite that
#    runs on top of TCP
#
#      make ibverbs_mplite
#         ./bin/mprun -np 2 -h host1 host2 NPibverbs ...
#      make memcpy_mplite
#
# There are also built in debug funtions dbprintf()
#
#      make mpi_debug
#
# I don't know if the modules below work anymore
#      shmem       : Measure SHMEM using shmem.c
#      ntcopy      : measure the memory copy rate of ntcopy
#      amdcopy     :
#
#      For more information, see the function printusage() in netpipe.c
########################################################################

CC         = gcc
ifdef MACOSX_DEPLOYMENT_TARGET
   CFLAGS     = -g -O3 -Wall
else
   CFLAGS     = -g -O3 -Wall -lrt
endif
SRC        = ./src

# Set -DNOCOLOR if you want to live in a dull and boring world
DEFS       =
#DEFS       = -DNOCOLOR

HOME       = /homes/daveturner

# mpicc will set up the proper include and library paths

MPICC       = mpicc
MPICHCC     = $(HOME)/libs/mpich/bin/mpicc
MVAPICHCC   = $(HOME)/libs/mvapich/bin/mpicc
OPENMPICC   = $(HOME)/libs/openmpi/bin/mpicc


all:mpi 

clean distclean:
	rm -f *.o NP* np.out MP_Lite/*.o
	(cd MP_Lite; make clean)

clear:
	rm -f netpipe.o* netpipe.po*


# This section of the Makefile is for compiling the binaries

mpi: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c -o NPmpi -I$(SRC)

mpi_debug: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c -o NPmpi -I$(SRC) -DDEBUG

openmpi: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(OPENMPICC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c -o NPmpi -I$(SRC)

mpich: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICHCC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c -o NPmpich -I$(SRC)

mvapich: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MVAPICHCC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c -o NPmvapich -I$(SRC)

# TCP compilating

tcp: $(SRC)/tcp.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/tcp.c -DTCP $(DEFS) -o NPtcp -I$(SRC)

tcp_debug: $(SRC)/tcp.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/tcp.c -DTCP $(DEFS) -o NPtcp -I$(SRC) -DDEBUG

tcp_mplite: $(SRC)/tcp.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	( cd ./MP_Lite; make; )
	$(CC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/tcp.c -DTCP $(DEFS) -o NPtcp -I$(SRC) \
				-I./MP_Lite ./MP_Lite/libmplite.a

# IB verbs

ibverbs: $(SRC)/ibverbs.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) $(SRC)/ibverbs.c $(SRC)/netpipe.c -o NPibverbs -DIBVERBS $(DEFS) -libverbs

ibverbs_debug: $(SRC)/ibverbs.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) $(SRC)/ibverbs.c $(SRC)/netpipe.c -o NPibverbs -DIBVERBS $(DEFS) -libverbs -DDEBUG 

ibverbs_mplite: $(SRC)/ibverbs.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	( cd ./MP_Lite; make; )
	$(CC) $(CFLAGS) $(SRC)/ibverbs.c $(SRC)/netpipe.c -o NPibverbs -DIBVERBS $(DEFS) -libverbs \
				-I./MP_Lite ./MP_Lite/libmplite.a


# IO module

disk: $(SRC)/disk.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/disk.c -DDISK $(DEFS) -o NPdisk -I$(SRC)

disk_debug: $(SRC)/disk.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/disk.c -DDISK $(DEFS) -o NPdisk -I$(SRC) -DDEBUG


# Theoretical model 

theo: $(SRC)/theo.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	$(CC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/theo.c -o NPtheo -DTHEO -I$(SRC)


# memcpy module

memcpy: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/memcpy.c $(DEFS) -o NPmemcpy -I$(SRC)

memcpy.icc: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	export OMPI_CC=icc; mpicc --version; $(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/memcpy.c $(DEFS) -o NPmemcpy.icc -I$(SRC)

memcpy_debug: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/memcpy.c $(DEFS) -o NPmemcpy -I$(SRC) -DDEBUG

memcpy_mplite: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	( cd ./MP_Lite; make; )
	$(CC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/memcpy.c $(DEFS) -o NPmemcpy -I$(SRC) \
				-I./MP_Lite ./MP_Lite/libmplite.a


# Everything below probably doesn't work

ntcopy: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h $(SRC)/opt_memcpy.c
	$(CC) $(CFLAGS) -mmmx -msse $(SRC)/netpipe.c $(SRC)/memcpy.c \
              $(SRC)/opt_memcpy.c -DUSE_OPT_MEMCPY -DNTCOPY $(DEFS) \
              -o NPntcopy -I$(SRC)

amdcopy: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h $(SRC)/opt_memcpy.c
	$(CC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/memcpy.c \
              $(SRC)/opt_memcpy.c $(SRC)/mmx_copy.s -DUSE_OPT_MEMCPY \
              -DAMDCOPY $(DEFS) -o NPamdcopy -I$(SRC)

pagecopy: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h $(SRC)/opt_memcpy.c
	$(CC) $(CFLAGS) -mmmx -msse $(SRC)/netpipe.c $(SRC)/memcpy.c \
              $(SRC)/opt_memcpy.c -DUSE_OPT_MEMCPY \
              -DPAGECOPY $(DEFS) -o NPpagecopy -I$(SRC)

mmxcopy: $(SRC)/memcpy.c $(SRC)/netpipe.c $(SRC)/netpipe.h $(SRC)/opt_memcpy.c
	$(CC) $(CFLAGS) -mmmx -msse $(SRC)/netpipe.c $(SRC)/memcpy.c \
              $(SRC)/opt_memcpy.c -DUSE_OPT_MEMCPY \
              -DMMXCOPY $(DEFS) -o NPmmxcopy -I$(SRC)

shmem: $(SRC)/shmem.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) $(SRC)/netpipe.c $(SRC)/shmem.c \
              -DSHMEM $(DEFS) -o NPshmem -I$(SRC) -lshmem

sync: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	( cd ~/mplite; make clean; make sync; )
	$(CC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c -o NPmplite \
         -I$(SRC) -I$(MP_Lite_home) $(MP_Lite_home)/libmplite.a

mpi3: $(SRC)/mpi3_shm.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi3_shm.c -o NPmpi3 -I$(SRC)

mpi2: $(SRC)/mpi2.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(MPICC) $(CFLAGS) -DMPI -DMPI2 $(DEFS) $(SRC)/netpipe.c \
           $(SRC)/mpi2.c -o NPmpi2 -I$(SRC)

mplite MP_Lite sigio: $(SRC)/mpi.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	( cd $(MP_Lite_home); make; )
	$(CC) $(CFLAGS) -DMPI $(DEFS) $(SRC)/netpipe.c $(SRC)/mpi.c \
            -o NPmplite -I$(SRC) -I$(MP_Lite_home) $(MP_Lite_home)/libmplite.a

kput kcopy_put: $(SRC)/kcopy_put.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(CC) $(CFLAGS) -DKCOPY $(DEFS) $(SRC)/netpipe.c \
           $(SRC)/kcopy_put.c -o NPkput

kpost kcopy_post: $(SRC)/kcopy_post.c $(SRC)/netpipe.c $(SRC)/netpipe.h 
	$(CC) $(CFLAGS) -DKCOPY $(DEFS) $(SRC)/netpipe.c \
           $(SRC)/kcopy_post.c -o NPkpost

sisci: $(SRC)/sisci.c $(SRC)/netpipe.c $(SRC)/netpipe.h
	$(CC) $(CFLAGS) -DSISCI $(SRC)/netpipe.c \
           $(SRC)/sisci.c -o NPsisci -I$(SRC) \
           -I/opt/DIS/include -I/opt/DIS/src/SCI_SOCKET/scilib/SISCI -L/opt/DIS/

