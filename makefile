# ============================================================================
#  SS 2025/2026 – glavni (jedini) makefile projekta
#
#    make          -> gradi assembler, linker i emulator u korenu projekta
#    make testa    -> build + pokretanje tests/nivo-a/start.sh
#    make testb    -> build + pokretanje tests/nivo-b/start.sh
#    make clean    -> uklanja sve generisane artefakte
#
#  Nakon `make` alati se koriste standardno, npr.:
#    ./assembler -o test.o test.s
#    ./linker -hex -place=my_code@0x40000000 -o program.hex test.o
#    ./emulator program.hex
# ============================================================================

SRC_DIR  = src
INC_DIR  = inc
MISC_DIR = misc
OBJ_DIR  = obj

CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -g -I$(INC_DIR)
BISON    = bison
FLEX     = flex

# ---- Objektni fajlovi po alatu -------------------------------------------
#  Svi objektni fajlovi žive u istom OBJ_DIR. objfile.o dele asembler i linker:
#  isti izvor + isti flegovi => jedan objekat koji se prevodi tačno jednom
#  (nema kolizije jer ne postoje dva različita pravila za isti .o).
ASM_OBJS = $(OBJ_DIR)/assembler.o $(OBJ_DIR)/objfile.o \
           $(OBJ_DIR)/parser.o    $(OBJ_DIR)/lexer.o
LNK_OBJS = $(OBJ_DIR)/linker.o    $(OBJ_DIR)/objfile.o
EMU_OBJS = $(OBJ_DIR)/emulator.o

# ---- Obavezna tri izvršna programa ---------------------------------------
all: assembler linker emulator

assembler: $(ASM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(ASM_OBJS)

linker: $(LNK_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(LNK_OBJS)

emulator: $(EMU_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(EMU_OBJS)

# ---- Opšte pravilo prevođenja: src/%.cpp -> obj/%.o ----------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- Zavisnosti od zaglavlja (korektno ponovno prevođenje) ---------------
$(OBJ_DIR)/assembler.o: $(INC_DIR)/assembler.hpp $(INC_DIR)/objfile.hpp
$(OBJ_DIR)/objfile.o:   $(INC_DIR)/objfile.hpp   $(INC_DIR)/assembler.hpp
$(OBJ_DIR)/linker.o:    $(INC_DIR)/linker.hpp    $(INC_DIR)/objfile.hpp $(INC_DIR)/assembler.hpp
$(OBJ_DIR)/emulator.o:  $(INC_DIR)/emulator.hpp
$(OBJ_DIR)/parser.o:    $(INC_DIR)/parser.hpp    $(INC_DIR)/assembler.hpp
$(OBJ_DIR)/lexer.o:     $(INC_DIR)/parser.hpp

# ---- Flex/Bison: parser i lexer se generišu kada je potrebno -------------
$(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp: $(MISC_DIR)/parser.y $(INC_DIR)/assembler.hpp
	$(BISON) -d --defines=$(INC_DIR)/parser.hpp -o $(SRC_DIR)/parser.cpp $(MISC_DIR)/parser.y

$(SRC_DIR)/lexer.cpp: $(MISC_DIR)/lexer.l $(INC_DIR)/parser.hpp
	$(FLEX) -o $(SRC_DIR)/lexer.cpp $(MISC_DIR)/lexer.l

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# ---- Testovi -------------------------------------------------------------
#  Najpre se izgrade sva tri alata, pa se pokreće NEIZMENJEN start.sh iz
#  odgovarajućeg test direktorijuma. PATH se proširuje korenom projekta samo
#  za taj poziv (bez trajne promene korisničkog PATH-a) da skripta nađe
#  komande assembler, linker i emulator.
testa: all
	@cd tests/nivo-a && PATH="$(CURDIR):$$PATH" bash start.sh

testb: all
	@cd tests/nivo-b && PATH="$(CURDIR):$$PATH" bash start.sh

# ---- Čišćenje -------------------------------------------------------------
#  Briše izvršne programe, objektne direktorijume (uklj. stari obj_linker),
#  generisani parser/lexer i sve generisane .o/.o.txt/.hex iz korena i test
#  direktorijuma. NE dira .s fajlove, start.sh, izvorni kod ni zaglavlja.
clean:
	rm -rf $(OBJ_DIR) obj_linker
	rm -f assembler linker emulator objreader
	rm -f $(SRC_DIR)/parser.cpp $(INC_DIR)/parser.hpp $(SRC_DIR)/lexer.cpp
	rm -f *.o *.o.txt *.hex
	rm -f tests/nivo-a/*.o tests/nivo-a/*.o.txt tests/nivo-a/*.hex
	rm -f tests/nivo-b/*.o tests/nivo-b/*.o.txt tests/nivo-b/*.hex

.PHONY: all clean testa testb
