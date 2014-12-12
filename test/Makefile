WORKDIR=.
TESTDIR=./test

include misc/mf-template.mk

test:$(TESTDIR)/test.out
	$(TESTDIR)/test.out

$(TESTDIR)/test.out:message.dt response.dt
	$(LINK) $(TESTDIR)/*.o utils/*.o core/*.o \
	        $(TEST_LIBS) \
	     -o $(TESTDIR)/test.out

clean:
	rm -f $(TESTDIR)/*.o
	rm -f $(TESTDIR)/*.out
