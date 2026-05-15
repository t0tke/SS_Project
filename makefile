TARGET = asembler

# Direktorijumi
SRC_DIR = src
INC_DIR = inc
MISC_DIR = misc
OBJ_DIR = obj

# Kompajler i flegovi (dodat -I$(INC_DIR) da bi video assembler.hpp)
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -g -I$(INC_DIR)

# Alati
BISON = bison
FLEX = flex

# Fajlovi
# Napomena: parser i lexer će generisati .cpp fajlove koje ćemo staviti u SRC_DIR
SRCS = $(SRC_DIR)/assembler.cpp $(SRC_DIR)/parser.cpp $(SRC_DIR)/lexer.cpp
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(TARGET)

# Pravljenje izvršnog fajla
$(TARGET): $(OBJ_DIR) $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Kompajliranje objektnih fajlova
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Generisanje parsera (iz misc u src)
$(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp: $(MISC_DIR)/parser.y $(INC_DIR)/assembler.hpp
	$(BISON) -d -o $(SRC_DIR)/parser.cpp $(MISC_DIR)/parser.y
	mv $(SRC_DIR)/parser.hpp $(INC_DIR)/parser.hpp 2>/dev/null || true

# Generisanje leksera (iz misc u src)
$(SRC_DIR)/lexer.cpp: $(MISC_DIR)/lexer.l $(INC_DIR)/parser.hpp
	$(FLEX) -o $(SRC_DIR)/lexer.cpp $(MISC_DIR)/lexer.l

# Kreiranje obj foldera ako ne postoji
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)


clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	rm -f $(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp $(SRC_DIR)/lexer.cpp izlaz.o

test: $(TARGET)
	@echo "--- Pokrećem test nivo-c ---"
	./$(TARGET) -o izlaz.o tests/nivo-c/main.s

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

.PHONY: all clean test