
# Build environment can be configured the following
# environment variables:
#   CC : Specify the C compiler to use
#   CFLAGS : Specify compiler options to use
#

CFLAGS += -I. -DPACKAGE_VERSION=\"1.0.0\"

LDFLAGS =
LDLIBS = -lcrex -ltidal -ldali -lslink -lmseed -lm

all: slgts msgts

slgts: slgts.o
	$(CC) $(CFLAGS) -o $@ slgts.o $(LDFLAGS) $(LDLIBS)

msgts: msgts.o
	$(CC) $(CFLAGS) -o $@ msgts.o $(LDFLAGS) $(LDLIBS)

clean:
	rm -f slgts.o slgts msgts.o msgts

# Implicit rule for building object files
%.o: %.c
	$(CC) $(CFLAGS) -c $<

install:
	@echo
	@echo "No install target, copy the executable(s) yourself"
	@echo
