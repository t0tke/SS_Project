TARGET = assembler

# Direktorijumi
SRC_DIR = src
INC_DIR = inc
MISC_DIR = misc
OBJ_DIR = obj

# Kompajler i flegovi
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -g -I$(INC_DIR)

# Alati
BISON = bison
FLEX = flex

# Fajlovi
SRCS = $(SRC_DIR)/assembler.cpp $(SRC_DIR)/parser.cpp $(SRC_DIR)/lexer.cpp
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(TARGET)

# Pravljenje izvršnog fajla (obj folder je stavljen iza | kao order-only zavisnost)
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Kompajliranje objektnih fajlova (svaki .o fajl traži da obj folder postoji)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Generisanje parsera
$(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp: $(MISC_DIR)/parser.y $(INC_DIR)/assembler.hpp
	$(BISON) -d -o $(SRC_DIR)/parser.cpp $(MISC_DIR)/parser.y
	mv $(SRC_DIR)/parser.hpp $(INC_DIR)/parser.hpp 2>/dev/null || true

# Generisanje leksera
$(SRC_DIR)/lexer.cpp: $(MISC_DIR)/lexer.l $(INC_DIR)/parser.hpp
	$(FLEX) -o $(SRC_DIR)/lexer.cpp $(MISC_DIR)/lexer.l

# Kreiranje obj foldera ako ne postoji
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	rm -f $(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp $(SRC_DIR)/lexer.cpp
	rm -f *_a.o *_b.o *_c.o

# Pravilo za Nivo A
testa: $(TARGET)
	@echo "--- Pokrećem testiranje nad nivo-a primerima ---"
	./$(TARGET) -o main_a.o tests/nivo-a/main.s
	./$(TARGET) -o math_a.o tests/nivo-a/math.s
	./$(TARGET) -o handler_a.o tests/nivo-a/handler.s
	./$(TARGET) -o isr_timer_a.o tests/nivo-a/isr_timer.s
	./$(TARGET) -o isr_terminal_a.o tests/nivo-a/isr_terminal.s
	./$(TARGET) -o isr_software_a.o tests/nivo-a/isr_software.s
	@echo "--- Uspešno asembliran nivo-a! Pogledaj .o fajlove u trenutnom folderu ---"

# Pravilo za Nivo B
testb: $(TARGET)
	@echo "--- Pokrećem testiranje nad nivo-b primerom ---"
	./$(TARGET) -o main_b.o tests/nivo-b/main.s
	@echo "--- Uspešno asembliran nivo-b! Pogledaj 'main_b.o' ---"

# Pravilo za Nivo C
testc: $(TARGET)
	@echo "--- Pokrećem testiranje nad nivo-c primerom ---"
	./$(TARGET) -o main_c.o tests/nivo-c/main.s
	@echo "--- Uspešno asembliran nivo-c! Pogledaj 'main_c.o' ---"

# Popravljeni .PHONY koji sada ukljucuje sva tvoja pravila
.PHONY: all clean testa testb testc