ifeq ($(MODE), debug)
	OPT_FLAGS=
else
	OPT_FLAGS=-O3 -D_ELPP_DISABLE_DEBUG_LOGS
endif

ifndef COMPILER
	COMPILER=clang++
endif

CC=$(COMPILER) -c -std=c++0x -D_XOPEN_SOURCE
INCLUDE=-I.
RESOLVE_DEP=$(COMPILER) -std=c++0x -MM $(INCLUDE)
LINK=$(COMPILER) -rdynamic
AR=ar rcs

CFLAGS=-Wall -Wextra -Wold-style-cast -Werror $(OPT_FLAGS)
MKTMP := $(shell mktemp)

LIBS_DIR=libs
LIBS=-L$(LIBS_DIR) -lpthread -lbacktracpp
WORK_LIBS=$(LIBS)
TEST_LIBS=-lgtest -lgtest_main $(LIBS)

COMPILE=$(CC) $(CFLAGS) $(INCLUDE)
COMPILE_GENERATED=$(CC) $(INCLUDE)

%.d:$(WORKDIR)/%.cpp
	@echo -n "$(WORKDIR)/" > $(MKTMP)
	@$(RESOLVE_DEP) $< >> $(MKTMP)
	@echo "	$(COMPILE) $< -o $(WORKDIR)/$*.o" >> $(MKTMP)
	@make -f $(MKTMP)

%.dt:$(TESTDIR)/%.cpp
	@echo -n "$(TESTDIR)/" > $(MKTMP)
	@$(RESOLVE_DEP) $< >> $(MKTMP)
	@echo "	$(COMPILE) $< -o $(TESTDIR)/$*.o" >> $(MKTMP)
	@make -f $(MKTMP)
