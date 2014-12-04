WORKDIR=.
TESTDIR=./test

include misc/mf-template.mk

test:$(TESTDIR)/test.out
	$(TESTDIR)/test.out

$(TESTDIR)/test.out:message.dt
	$(LINK) $(TESTDIR)/*.o \
	        $(WORKDIR)/exceptions.o \
	        $(TEST_LIBS) \
	     -o $(TESTDIR)/test.out

clean:
	rm -f $(TESTDIR)/*.o
	rm -f $(TESTDIR)/*.out
