## Flags etc.
CXX := g++
BOOSTINCLUDE := -I/usr/local/boost_1_77_0
LOCALINCLUDE := -I./include
ifeq ($(DEBUG),1)
	DEBUGFLAGS := -ggdb
else
	DEBUGFLAGS :=
endif
CXXFLAGS := -std=c++17 $(BOOSTINCLUDE) $(LOCALINCLUDE) $(DEBUGFLAGS)
LDFLAGS := -pthread

## Dirs
BUILDDIR := build
SRCDIR := src
TESTDIR := test
FTPDIR := ftp
IODIR := io
FSMDIR := fsm

## Run target
run: main
	./$(MAINBIN)

## Clean target
clean:
	rm -r $(BUILDDIR)

## CommandFsm.cpp targets
COMMANDFSMCPP := $(SRCDIR)/$(FSMDIR)/CommandFsm.cpp
COMMANDFSMOBJ := $(BUILDDIR)/$(FSMDIR)/CommandFsm.obj

$(COMMANDFSMOBJ) : $(COMMANDFSMCPP)
	mkdir -p $(BUILDDIR)/$(FSMDIR)
	$(CXX) -c $(CXXFLAGS) $(COMMANDFSMCPP) -o $@

## Socket.cpp targets
SOCKETCPP := $(SRCDIR)/$(IODIR)/Socket.cpp
SOCKETOBJ := $(BUILDDIR)/$(IODIR)/Socket.o

$(SOCKETOBJ) : $(SOCKETCPP)
	mkdir -p $(BUILDDIR)/$(IODIR)
	$(CXX) -c $(CXXFLAGS) $(SOCKETCPP) -o $@

## Client.cpp targets
CLIENTCPP := $(SRCDIR)/$(FTPDIR)/Client.cpp
CLIENTOBJ := $(BUILDDIR)/$(FTPDIR)/Client.o

$(CLIENTOBJ): $(CLIENTCPP)
	mkdir -p $(BUILDDIR)/$(FTPDIR)
	$(CXX) -c $(CXXFLAGS) $(CLIENTCPP) -o $@

## main.cpp targets
MAINCPP := $(SRCDIR)/main.cpp
MAINBIN := $(BUILDDIR)/main.a

$(MAINBIN): $(MAINCPP) $(CLIENTOBJ) $(SOCKETOBJ) $(COMMANDFSMOBJ)
	mkdir -p $(BUILDDIR)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $^ -o $@

main: $(MAINBIN)

## Client functional test targets
CLIENTFUNCTIONALTESTCPP := $(TESTDIR)/$(FTPDIR)/ClientFunctionalTests.cpp
CLIENTFUNCTIONALTESTBIN := $(BUILDDIR)/$(FTPDIR)/ClientFunctionalTests.a

$(CLIENTFUNCTIONALTESTBIN): $(CLIENTFUNCTIONALTESTCPP) $(CLIENTOBJ) $(SOCKETOBJ) $(COMMANDFSMOBJ)
	mkdir -p $(BUILDDIR)/$(FTPDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

## All test targets
test: $(CLIENTFUNCTIONALTESTBIN)
	./$(CLIENTFUNCTIONALTESTBIN)
