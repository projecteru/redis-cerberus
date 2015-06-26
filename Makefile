ifeq ($(STATIC_LINK), 1)
	SLINK=-static-libstdc++
else
	SLINK=
endif

WORKDIR=.
OBJDIR=./build

include misc/mf-template.mk

all:main_exec
	@echo "Done"

main_exec:core_objs utilities libs_3rdparty main.d
	$(LINK) utils/*.o $(OBJDIR)/*.o $(WORK_LIBS) $(SLINK) -o cerberus

runtest:main_exec utilities libs_3rdparty
	@make -f test/Makefile MODE=$(MODE) COMPILER=$(COMPILER) \
	                       CHECK_MEM=$(CHECK_MEM)

utilities:
	@make -f utils/Makefile MODE=$(MODE) COMPILER=$(COMPILER)

kill-test-server:
	@python test/cluster_launcher.py kill

core_objs:
	@mkdir -p $(OBJDIR)
	@make -f core/Makefile OBJDIR=$(OBJDIR) MODE=$(MODE) COMPILER=$(COMPILER)

libs_3rdparty:
	@mkdir -p $(LIBS_DIR)
	@make -f backtracpp/Makefile LIB_DIR=$(LIBS_DIR) REL_PATH=backtracpp

clean:
	find -type f -name "*.o" -exec rm {} \;
	rm -f cerberus
	rm -f test/*.out
	rm -rf $(LIBS_DIR)
