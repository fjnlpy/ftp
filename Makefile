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
	rm -r $(BUILDDIR)

## Client.cpp targets
CLIENTCPP := $(SRCDIR)/$(FTPDIR)/Client.cpp
CLIENTOBJ := $(BUILDDIR)/$(FTPDIR)/Client.o

$(CLIENTOBJ): $(CLIENTCPP)
	mkdir -p $(BUILDDIR)/$(FTPDIR)
	$(CXX) -c $(CXXFLAGS) $(CLIENTCPP) -o $@

## main.cpp targets
MAINCPP := $(SRCDIR)/main.cpp
MAINBIN := $(BUILDDIR)/main.a

$(MAINBIN): $(MAINCPP) $(CLIENTOBJ)
	mkdir -p $(BUILDDIR)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $(MAINCPP) $(CLIENTOBJ) -o $@

main: $(MAINBIN)
