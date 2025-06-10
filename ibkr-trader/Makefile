CXX = g++
CXXFLAGS = -std=c++20 -w -g
INCLUDES = -I./include \
           -I./include/ibkr/cppclient/client \
           -I./include/ibkr/cppclient/client/protobufUnix \
           -I./include/nlohmann \
           -I./src
SRCDIR = src
OBJDIR = obj
BINDIR = bin
LIBDIR = lib
TESTDIR = $(SRCDIR)/tests

# Link against protobuf library
LIBS = -L$(LIBDIR) -lprotobuf -pthread -lbid -lz

# Add IBKR source files from include directory
IBKR_SRCDIR = include/ibkr/cppclient/client
SOURCES = $(wildcard $(SRCDIR)/*.cpp) \
          $(wildcard $(SRCDIR)/*/*.cpp) \
          $(wildcard $(SRCDIR)/*/*/*.cpp) \
          $(wildcard $(IBKR_SRCDIR)/*.cpp) \
          $(wildcard $(IBKR_SRCDIR)/protobufUnix/*.cc) \
          util/app_state.cpp \
          util/stk_q/stk_q.cpp \
          util/time_ordered_tick_buffer/time_ordered_tick_buffer.cpp \
          util/time_ordered_tick_buffer/ring_buffer_trade_handler.cpp \
          util/time_ordered_tick_buffer/ring_buffer_monitor.cpp \
          util/training_data_collector/training_data_collector.cpp

# Exclude official_example_connections directory
SOURCES := $(filter-out $(SRCDIR)/connection_manager/official_example_connections/%.cpp, $(SOURCES))
          
# Update OBJECTS to handle both src and include files
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(filter $(SRCDIR)/%.cpp,$(SOURCES))) \
          $(patsubst $(IBKR_SRCDIR)/%.cpp,$(OBJDIR)/ibkr/%.o,$(filter $(IBKR_SRCDIR)/%.cpp,$(SOURCES))) \
          $(patsubst $(IBKR_SRCDIR)/protobufUnix/%.cc,$(OBJDIR)/ibkr/protobuf/%.o,$(filter $(IBKR_SRCDIR)/protobufUnix/%.cc,$(SOURCES))) \
          $(patsubst util/%.cpp,$(OBJDIR)/util/%.o,$(filter util/%.cpp,$(SOURCES)))
          
TARGET = $(BINDIR)/ibkr-trader

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Test targets using TestInputManager
test-cli: $(TARGET)
	@echo "Running test CLI mode (using TestInputManager)..."
	@$(TARGET) --test-cli

test-api: $(TARGET)
	@echo "Running test API mode (using TestInputManager)..."
	@$(TARGET) --test-api

# Direct mode targets using InputManager
run-cli: $(TARGET)
	@echo "Running CLI mode directly (using InputManager)..."
	@$(TARGET) --cli
	
run-api: $(TARGET)
	@echo "Running API server directly (using InputManager)..."
	@$(TARGET) --api

# Help target
help: $(TARGET)
	@echo "Showing help..."
	@$(TARGET) --help

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

# Rule for util files
$(OBJDIR)/util/%.o: util/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Thread Sanitizer build
# tsan: CXXFLAGS = -std=c++20 -O2 -g -fsanitize=thread
# tsan: LIBS += -fsanitize=thread
# tsan: clean $(TARGET)

.PHONY: all clean test-cli test-api run-cli run-api help #tsan