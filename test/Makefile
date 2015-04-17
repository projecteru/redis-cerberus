WORKDIR=.
TESTDIR=./test

include misc/mf-template.mk

ifeq ($(CHECK_MEM), 1)
	VALGRIND=valgrind
else
	VALGRIND=
endif

MOCK_OBJS=$(TESTDIR)/mock-stats.o

test:util-test slot-map-test
	@echo "======================"
	@echo "| Test done _(:3J<)_ |"
	@echo "======================"

util-test:message.dt response.dt buffer.dt slot_calc.dt
	$(LINK) $(TESTDIR)/message.o $(TESTDIR)/response.o $(TESTDIR)/buffer.o \
	        $(TESTDIR)/slot_calc.o \
	        utils/*.o core/*.o $(TEST_LIBS) \
	     -o $(TESTDIR)/test.out
	$(VALGRIND) $(TESTDIR)/test.out

slot-map-test:slot_map.dt mock-server.dt mock-suit
	$(LINK) $(TESTDIR)/mock-server.o $(TESTDIR)/slot_map.o utils/*.o \
	        core/slot_map.o core/exceptions.o core/connection.o core/fdutil.o \
	        $(TEST_LIBS) $(MOCK_OBJS) \
	     -o $(TESTDIR)/test-slot-map.out
	$(VALGRIND) $(TESTDIR)/test-slot-map.out

mock-suit:mock-stats.dt
	@true

clean:
	rm -f $(TESTDIR)/*.o
	rm -f $(TESTDIR)/*.out
