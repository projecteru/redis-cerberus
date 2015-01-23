WORKDIR=.
TESTDIR=./test

include misc/mf-template.mk

ifeq ($(CHECK_MEM), 1)
	VALGRIND=valgrind
else
	VALGRIND=
endif

test:$(TESTDIR)/test.out
	$(VALGRIND) $(TESTDIR)/test.out

$(TESTDIR)/test.out:message.dt response.dt buffer.dt
	$(LINK) $(TESTDIR)/*.o utils/*.o core/*.o \
	        $(TEST_LIBS) \
	     -o $(TESTDIR)/test.out

clean:
	rm -f $(TESTDIR)/*.o
	rm -f $(TESTDIR)/*.out
