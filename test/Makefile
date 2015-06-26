WORKDIR=./
OBJDIR=./test/core_objs
TESTDIR=./test

include misc/mf-template.mk

ifeq ($(CHECK_MEM), 1)
	VALGRIND=valgrind
else
	VALGRIND=
endif

MOCK_OBJS=$(TESTDIR)/mock-stats.o $(TESTDIR)/mock-io.o $(TESTDIR)/mock-poll.o \
          $(TESTDIR)/mock-acceptor.o

test:core-objs util-test buffer-test slot-map-test server-client-test \
     event-loop-test script-test
	@echo "======================"
	@echo "| Test done _(:3J<)_ |"
	@echo "======================"

core-objs:
	@mkdir -p $(OBJDIR)
	@make -f core/Makefile OBJDIR=$(OBJDIR) MODE=$(MODE) \
	    COMPILER=$(COMPILER) CANDIDATE_IO=1 CANDIDATE_POLL=1 CANDIDATE_FCTL=1

util-test:message.dt response.dt buffer.dt slot_calc.dt mock-io.dt mock-suit \
          mock-server.dt mock-proxy.dt
	$(LINK) $(TESTDIR)/message.o $(TESTDIR)/response.o $(TESTDIR)/slot_calc.o \
	        $(OBJDIR)/buffer.o $(OBJDIR)/slot_calc.o $(OBJDIR)/message.o \
	        $(OBJDIR)/slot_map.o $(OBJDIR)/response.o $(OBJDIR)/connection.o \
	        $(OBJDIR)/fdutil.o utils/*.o $(TESTDIR)/mock-proxy.o $(MOCK_OBJS) \
	        $(TESTDIR)/mock-server.o $(TEST_LIBS) \
	     -o $(TESTDIR)/test-utils.out
	$(VALGRIND) $(TESTDIR)/test-utils.out

buffer-test:buffer.dt mock-suit mock-server.dt mock-proxy.dt
	$(LINK) $(TESTDIR)/buffer.o $(OBJDIR)/buffer.o $(OBJDIR)/connection.o \
	        $(OBJDIR)/fdutil.o $(OBJDIR)/slot_map.o $(TESTDIR)/mock-proxy.o \
	        utils/*.o $(TESTDIR)/mock-server.o $(MOCK_OBJS) $(TEST_LIBS) \
	     -o $(TESTDIR)/test-buffer.out
	$(VALGRIND) $(TESTDIR)/test-buffer.out

slot-map-test:slot_map.dt mock-server.dt mock-proxy.dt mock-suit
	$(LINK) $(TESTDIR)/mock-server.o $(TESTDIR)/slot_map.o $(OBJDIR)/buffer.o \
	        $(OBJDIR)/slot_map.o $(OBJDIR)/fdutil.o $(OBJDIR)/connection.o \
	        utils/*.o $(TESTDIR)/mock-proxy.o $(MOCK_OBJS) $(TEST_LIBS) \
	     -o $(TESTDIR)/test-slot-map.out
	$(VALGRIND) $(TESTDIR)/test-slot-map.out

server-client-test:server-client.dt mock-proxy.dt mock-suit
	$(LINK) $(TESTDIR)/server-client.o $(OBJDIR)/globals.o $(OBJDIR)/buffer.o \
	     $(OBJDIR)/connection.o $(OBJDIR)/server.o $(OBJDIR)/client.o \
	     $(OBJDIR)/fdutil.o $(OBJDIR)/response.o $(OBJDIR)/command.o \
	     $(OBJDIR)/subscription.o $(OBJDIR)/message.o $(OBJDIR)/slot_calc.o \
	     $(OBJDIR)/slot_map.o utils/*.o $(TESTDIR)/mock-proxy.o $(MOCK_OBJS) \
	     $(TEST_LIBS) \
	  -o $(TESTDIR)/test-server-client.out
	$(VALGRIND) $(TESTDIR)/test-server-client.out

event-loop-test:event-loop-test.dt event-loop-data-proxy.dt mock-suit
	$(LINK) $(TESTDIR)/event-loop-test.o $(TESTDIR)/event-loop-data-proxy.o \
	     utils/*.o $(MOCK_OBJS) \
	     $(OBJDIR)/connection.o $(OBJDIR)/server.o $(OBJDIR)/client.o \
	     $(OBJDIR)/fdutil.o $(OBJDIR)/response.o $(OBJDIR)/command.o \
	     $(OBJDIR)/subscription.o $(OBJDIR)/message.o $(OBJDIR)/globals.o \
	     $(OBJDIR)/buffer.o $(OBJDIR)/slot_calc.o $(OBJDIR)/slot_map.o \
	     $(OBJDIR)/proxy.o $(TEST_LIBS) \
	  -o $(TESTDIR)/test-event-loop.out
	$(VALGRIND) $(TESTDIR)/test-event-loop.out

script-test:
	@python test/script_test.py

mock-suit:mock-stats.dt mock-io.dt mock-poll.dt mock-acceptor.dt
	@true

clean:
	rm -f $(TESTDIR)/*.o
	rm -f $(TESTDIR)/*.out
	rm -f $(TESTDIR)/core_objs/*.o
