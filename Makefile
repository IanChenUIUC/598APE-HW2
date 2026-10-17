CXX := g++
CXXFLAGS := -std=c++17 -Ofast -Wall -Wextra -I include -I.

# Directories
SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj
BENCH_DIR := benchmark

# Chez Scheme
CHEZ_DIR := ChezScheme
CHEZ_MACHINE := ta6le
CHEZ_LIB := $(CHEZ_DIR)/$(CHEZ_MACHINE)/boot/$(CHEZ_MACHINE)

# Evaluate programs with Chez Scheme instead of the interpreter
CHEZ ?= 0

# Find all cpp files in src directory
SRCS := $(wildcard $(SRC_DIR)/*.cpp)

ifeq ($(CHEZ),1)
CXXFLAGS += -DUSE_CHEZ -DCHEZ_BOOT='"$(CHEZ_LIB)"' -I $(CHEZ_LIB)
LDLIBS := $(CHEZ_LIB)/libkernel.a $(CHEZ_DIR)/$(CHEZ_MACHINE)/zlib/libz.a \
	$(CHEZ_DIR)/$(CHEZ_MACHINE)/lz4/lib/liblz4.a -lm -ldl -lrt -lpthread -rdynamic
else
SRCS := $(filter-out $(SRC_DIR)/chez.cpp,$(SRCS))
endif

# Generate object file names
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Benchmark program
BENCH_SRC := $(BENCH_DIR)/genetic_benchmark.cpp
BENCH_BIN := genetic_benchmark

# Default target
all: directories $(CHEZ_LIB)/libkernel.a $(BENCH_BIN)

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BENCH_DIR)

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# build benchmark lib 
$(BENCH_BIN): $(BENCH_SRC) $(OBJS)
	@echo "Building benchmark program..."
	@$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDLIBS) -o $@

# Build Chez Scheme
$(CHEZ_LIB)/libkernel.a:
	cd $(CHEZ_DIR) && ./configure --as-is --disable-x11 --disable-curses && $(MAKE)

ifeq ($(CHEZ),1)
$(OBJ_DIR)/chez.o $(BENCH_BIN): $(CHEZ_LIB)/libkernel.a
endif

# Profile a workload
%.perf: all
	perf record -F 999 --call-graph dwarf -o $@ ./$(BENCH_BIN) $* > /dev/null

# Clean build files
clean:
	@echo "Cleaning build files..."
	@rm -rf $(OBJ_DIR) $(BENCH_BIN)

# Clean and rebuild
rebuild: clean all

.PHONY: all clean rebuild directories 
