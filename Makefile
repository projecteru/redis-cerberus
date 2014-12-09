WORKDIR=.

include misc/mf-template.mk

all:a.d objs utilities
	$(LINK) *.o utils/*.o \
	        $(LIBS) \
	     -o cerberus

runtest:objs utils
	make -f test/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

objs:concurrence.d buffer.d command.d response.d proxy.d exceptions.d
	true

utilities:
	make -f utils/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

clean:
	rm -f tmp.*
	find -type f -name "*.o" -exec rm {} \;
	rm -f cerberus
