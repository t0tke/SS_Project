#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <termios.h>

class Emulator {
public:
    int run(int argc, char* argv[]);

private:
    uint32_t gpr[16] = {0};  
    uint32_t csr[3]  = {0};   
    bool     running = true;

    std::unordered_map<uint32_t, uint8_t> mem_;

    // Terminal 
    uint32_t termIn_      = 0;  
    bool     termPending_ = false; 
    bool     termiosChanged_ = false; 
    termios  savedTermios_{};    

    uint32_t load32(uint32_t addr);
    void     store32(uint32_t addr, uint32_t value);

    // Tok emulacije
    bool loadHex(const std::string& path);
    void step();                     // izvrši jednu instrukciju
    void enterInterrupt(uint32_t cause);
    void checkTerminalInput();
    void printProcessorState() const;

    void termInit();
    void termRestore();

    void setGpr(uint32_t i, uint32_t v) { if (i != 0) gpr[i] = v; }
};
