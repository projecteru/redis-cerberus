WORKDIR=.

include misc/mf-template.mk

all:a.d objs
	$(LINK) *.o \
	        $(LIBS) \
	     -o cerberus

objs:concurrence.d proxy.d exceptions.d
	true

runtest:objs
	make -f test/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

clean:
	rm -f tmp.*
	find -type f -name "*.o" -exec rm {} \;
	rm -f cerberus
