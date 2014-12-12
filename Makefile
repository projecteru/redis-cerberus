WORKDIR=.

include misc/mf-template.mk

all:main.d core_objs utilities
	$(LINK) main.o utils/*.o core/*.o \
	        $(LIBS) \
	     -o cerberus

runtest:core_objs utilities
	make -f test/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

utilities:
	make -f utils/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

core_objs:
	make -f core/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

clean:
	rm -f tmp.*
	find -type f -name "*.o" -exec rm {} \;
	rm -f cerberus
