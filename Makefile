RANLIB=ranlib

LIBSRC=uthreads.cpp
LIBOBJ=$(LIBSRC:.cpp=.o)

INCS=-I.
CPPFLAGS = -Wall -g $(INCS)
LOADLIBES = -L./ 

UTHREADSLIB = libuthreads.a
TARGETS = $(UTHREADSLIB)

TAR=tar
TARFLAGS=-cvf
TARNAME=ex2.tar
TARSRCS=$(LIBSRC) Makefile README

all: $(TARGETS) 
	$(CXX) $(CPPFLAGS) -c -o uthreads.o uthreads.cpp

$(TARGETS): $(LIBOBJ)
	$(AR) $(ARFLAGS) $@ $^
	$(RANLIB) $@


clean:
	$(RM) $(TARGETS) $(OBJ) $(LIBOBJ) $(TARNAME) *~ *core


depend:
	makedepend -- $(CFLAGS) -- $(SRC) $(LIBSRC)


tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
