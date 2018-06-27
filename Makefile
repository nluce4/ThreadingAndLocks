CC=gcc
CFLAGS=-Wall

BINS=libmythreads.a

all: $(BINS)

mythreads.o: mythreads.c
	$(CC) $(CFLAGS) -c mythreads.c

libmythreads.a: mythreads.o
	ar -cvr libmythreads.a mythreads.o
	rm -rf *.o

clean:
	rm -rf $(BINS)
