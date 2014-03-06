TESTS    = barrier_performance.c lat_bw.c mac_sections.c put_performance.c test_sheap.c get_performance.c likely_macro.c osu_oshm_put_mr.c test_etext.c test_start.c
OBJECTS  = $(TESTS:.c=.o)
BINARIES = $(TESTS:.c=.x)
MACCRAP  = $(BINARIES:.x=.x.dSYM)

RUNCMD	 = mpiexec 
OPTIONS  = -np
NPROCS   = 2

CC      = mpicc
CFLAGS  = -g -O3 -std=c99 -I/home/sg/builds/oshmpi/include
LDFLAGS = $(CFLAGS) -Wl,-rpath=/home/sg/builds/oshmpi/lib -L/home/sg/builds/oshmpi/lib -lshmem -lm

all: $(BINARIES)

%.o: %.c
	$(CC) $(CFLAGS) -c $<            -o $@

%.x: %.o
	$(CC) $(CFLAGS)    $< $(LDFLAGS) -o $@

clean:
	-rm -f  $(OBJECTS)
	-rm -fr $(MACCRAP)
	-rm -f *.x

run :
	for bin in $(BINARIES); do \
		echo "-----------------------" ; \
		echo "TEST "$$bin ; \
		$(RUNCMD) -np $(NPROCS) ./$$bin; \
		echo "-----------------------" ; \
	done

realclean: clean
	-rm -f  $(BINARIES)
