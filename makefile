TARGET = assembler

SRC_DIR = src
INC_DIR = inc
MISC_DIR = misc
OBJ_DIR = obj

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -g -I$(INC_DIR)

BISON = bison
FLEX = flex

SRCS = $(SRC_DIR)/assembler.cpp $(SRC_DIR)/objfile.cpp $(SRC_DIR)/parser.cpp $(SRC_DIR)/lexer.cpp
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Pomoćni alat za round-trip proveru binarnog predmetnog fajla.
READER = objreader
READER_OBJS = $(OBJ_DIR)/objreader.o $(OBJ_DIR)/objfile.o

all: $(TARGET) $(READER)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

$(READER): $(READER_OBJS)
	$(CXX) $(CXXFLAGS) -o $(READER) $(READER_OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp: $(MISC_DIR)/parser.y $(INC_DIR)/assembler.hpp
	$(BISON) -d --defines=$(INC_DIR)/parser.hpp -o $(SRC_DIR)/parser.cpp $(MISC_DIR)/parser.y

$(SRC_DIR)/lexer.cpp: $(MISC_DIR)/lexer.l $(INC_DIR)/parser.hpp
	$(FLEX) -o $(SRC_DIR)/lexer.cpp $(MISC_DIR)/lexer.l

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(READER)
	rm -f $(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp $(SRC_DIR)/lexer.cpp
	rm -f *.o *.o.txt *.hex

testa: $(TARGET)
	@echo "--- Nivo A ---"
	./$(TARGET) -o main_a.o          tests/nivo-a/main.s
	./$(TARGET) -o math_a.o          tests/nivo-a/math.s
	./$(TARGET) -o handler_a.o       tests/nivo-a/handler.s
	./$(TARGET) -o isr_timer_a.o     tests/nivo-a/isr_timer.s
	./$(TARGET) -o isr_terminal_a.o  tests/nivo-a/isr_terminal.s
	./$(TARGET) -o isr_software_a.o  tests/nivo-a/isr_software.s
	@echo "--- Uspešno! ---"

testb: $(TARGET)
	@echo "--- Nivo B ---"
	./$(TARGET) -o main_b.o          tests/nivo-b/main.s
	./$(TARGET) -o handler_b.o       tests/nivo-b/handler.s
	./$(TARGET) -o isr_timer_b.o     tests/nivo-b/isr_timer.s
	./$(TARGET) -o isr_terminal_b.o  tests/nivo-b/isr_terminal.s
	@echo "--- Uspešno! ---"

testc: $(TARGET)
	@echo "--- Nivo C ---"
	./$(TARGET) -o main_c.o          tests/nivo-c/main.s
	./$(TARGET) -o handler_c.o       tests/nivo-c/handler.s
	./$(TARGET) -o isr_timer_c.o     tests/nivo-c/isr_timer.s
	./$(TARGET) -o isr_terminal_c.o  tests/nivo-c/isr_terminal.s
	@echo "--- Uspešno! ---"

.PHONY: all clean testa testb testc