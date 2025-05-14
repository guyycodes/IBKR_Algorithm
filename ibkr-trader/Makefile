CXX = g++
CXXFLAGS = -std=c++17 -w -g
INCLUDES = -I./include \
           -I./include/ibkr/cppclient/client \
           -I./include/ibkr/cppclient/client/protobufUnix \
           -I./src
SRCDIR = src
OBJDIR = obj
BINDIR = bin
LIBDIR = lib

# Link against protobuf library
LIBS = -L$(LIBDIR) -lprotobuf -pthread -lbid

# Add IBKR source files from include directory
IBKR_SRCDIR = include/ibkr/cppclient/client
SOURCES = $(wildcard $(SRCDIR)/*.cpp) \
          $(wildcard $(SRCDIR)/*/*.cpp) \
          $(wildcard $(IBKR_SRCDIR)/*.cpp) \
          $(wildcard $(IBKR_SRCDIR)/protobufUnix/*.cc)
          
# Update OBJECTS to handle both src and include files
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(filter $(SRCDIR)/%.cpp,$(SOURCES))) \
          $(patsubst $(IBKR_SRCDIR)/%.cpp,$(OBJDIR)/ibkr/%.o,$(filter $(IBKR_SRCDIR)/%.cpp,$(SOURCES))) \
          $(patsubst $(IBKR_SRCDIR)/protobufUnix/%.cc,$(OBJDIR)/ibkr/protobuf/%.o,$(filter $(IBKR_SRCDIR)/protobufUnix/%.cc,$(SOURCES)))
          
TARGET = $(BINDIR)/ibkr-trader

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Rule for src files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Rule for IBKR files
$(OBJDIR)/ibkr/%.o: $(IBKR_SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Rule for protobuf files
$(OBJDIR)/ibkr/protobuf/%.o: $(IBKR_SRCDIR)/protobufUnix/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: all clean