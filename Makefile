ifeq ($(STATIC_LINK), 1)
	SLINK=-static-libstdc++
else
	SLINK=
endif

WORKDIR=.

include misc/mf-template.mk

all:main.d core_objs utilities libs_3rdparty
	$(LINK) main.o utils/*.o core/*.o \
	        $(WORK_LIBS) $(SLINK) \
	     -o cerberus
	@echo "Done"

runtest:core_objs utilities libs_3rdparty
	rm -f tmp.*.txt
	@make -f test/Makefile MODE=$(MODE) COMPILER=$(COMPILER) \
	                       CHECK_MEM=$(CHECK_MEM)

utilities:
	@make -f utils/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

core_objs:
	@make -f core/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

libs_3rdparty:
	@mkdir -p $(LIBS_DIR)
	@make -f backtracpp/Makefile LIB_DIR=$(LIBS_DIR) REL_PATH=backtracpp

clean:
	rm -f tmp.*
	find -type f -name "*.o" -exec rm {} \;
	rm -f cerberus
	rm -rf $(LIBS_DIR)
