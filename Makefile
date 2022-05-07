## Flags etc.
CXX := g++
BOOSTINCLUDE := -I/usr/local/boost_1_77_0
LOCALINCLUDE := -I./include
CXXFLAGS := -std=c++17 $(BOOSTINCLUDE) $(LOCALINCLUDE)
LDFLAGS := -pthread

## Dirs
BUILDDIR := build
SRCDIR := src
FTPDIR := ftp

# Run target
run: main
	./$(MAINBIN)

## Clean target
clean:
	rm -r build

## Dir targets
# TODO: apparently not meant to make an explicit target
#  for directory creation.
$(BUILDDIR):
	mkdir -p $@

BUILDFTPDIR := $(BUILDDIR)/$(FTPDIR)
$(BUILDFTPDIR): $(BUILDDIR)
	mkdir -p $@

## Client.cpp targets
CLIENTCPP := $(SRCDIR)/$(FTPDIR)/Client.cpp
CLIENTOBJ := $(BUILDDIR)/$(FTPDIR)/Client.o

$(CLIENTOBJ): $(BUILDFTPDIR) $(CLIENTCPP)
	$(CXX) -c $(CXXFLAGS) $(CLIENTCPP) -o $@

## main.cpp targets
MAINCPP := $(SRCDIR)/main.cpp
MAINBIN := $(BUILDDIR)/main.a

$(MAINBIN): $(BUILDDIR) $(MAINCPP) $(CLIENTOBJ)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $(MAINCPP) $(CLIENTOBJ) -o $@

main: $(MAINBIN)
