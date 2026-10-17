CXX := clang++
CXXFLAGS := -std=c++17 -Ofast -Wall -Wextra -I include -I.

# Directories
SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj
BENCH_DIR := benchmark

# Find all cpp files in src directory
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
# Generate object file names
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Benchmark program
BENCH_SRC := $(BENCH_DIR)/genetic_benchmark.cpp
BENCH_BIN := genetic_benchmark

# Default target
all: directories $(BENCH_BIN) 

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BENCH_DIR)

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Copy-and-patch stencils
JIT_DIR := jit
STENCIL_FLAGS := -std=c++17 -O3 -mavx2 -ffp-contract=off -ffunction-sections \
	-fno-pic -mcmodel=medium -mlarge-data-threshold=0 -fno-stack-protector \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -I include -I $(SRC_DIR)

$(OBJ_DIR)/stencils.o: $(JIT_DIR)/stencils.cpp
	@echo "Compiling $<..."
	@$(CXX) $(STENCIL_FLAGS) -c $< -o $@

$(JIT_DIR)/stencils.h: $(OBJ_DIR)/stencils.o $(JIT_DIR)/extract.py
	@echo "Extracting stencils..."
	@python3 $(JIT_DIR)/extract.py $< $@

$(OBJ_DIR)/jit.o: $(JIT_DIR)/stencils.h

# build benchmark lib
$(BENCH_BIN): $(BENCH_SRC) $(OBJS)
	@echo "Building benchmark program..."
	@$(CXX) $(CXXFLAGS) $< $(OBJS) -o $@ 

# Profile a workload
%.perf: all
	perf record -F 999 --call-graph dwarf -o $@ ./$(BENCH_BIN) $* > /dev/null

# Clean build files
clean:
	@echo "Cleaning build files..."
	@rm -rf $(OBJ_DIR) $(BENCH_BIN) $(JIT_DIR)/stencils.h

# Clean and rebuild
rebuild: clean all

.PHONY: all clean rebuild directories 
