#include "emulator.hpp"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {
    constexpr uint32_t RESET_PC   = 0x40000000u;  
    constexpr uint32_t TERM_OUT   = 0xFFFFFF00u;    
    constexpr uint32_t TERM_IN    = 0xFFFFFF04u;    

    constexpr int CSR_STATUS_INDEX  = 0;
    constexpr int CSR_HANDLER_INDEX    = 1;
    constexpr int CSR_CAUSE_INDEX   = 2;

    constexpr uint32_t ST_TL = 0x2; 
    constexpr uint32_t ST_I  = 0x4;  

    // Uzroci prekida
    constexpr uint32_t CAUSE_BAD_INSTR = 1;  
    constexpr uint32_t CAUSE_TERMINAL  = 3;  
    constexpr uint32_t CAUSE_SOFTWARE  = 4;   
}

uint32_t Emulator::load32(uint32_t addr) {
    if (addr == TERM_IN)  return termIn_;
    if (addr == TERM_OUT) return 0;  

    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        auto it = mem_.find(addr + i);
        if (it != mem_.end()) v |= (uint32_t)it->second << (8 * i);
    }
    return v;
}

void Emulator::store32(uint32_t addr, uint32_t value) {
    if (addr == TERM_OUT) {
        unsigned char ch = (unsigned char)(value & 0xFF);
        (void)::write(STDOUT_FILENO, &ch, 1);
        return;
    }
    if (addr == TERM_IN) {
        return;
    }
    for (int i = 0; i < 4; i++)
        mem_[addr + i] = (uint8_t)((value >> (8 * i)) & 0xFF);
}


bool Emulator::loadHex(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "Error: ne mogu da otvorim ulaznu datoteku '%s'\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue; 

        std::string addrStr = line.substr(0, colon);
        char* endp = nullptr;
        unsigned long addr = std::strtoul(addrStr.c_str(), &endp, 16);
        if (*endp != '\0') {
            std::fprintf(stderr, "Error: neispravna adresa u -hex zapisu: '%s'\n", line.c_str());
            return false;
        }

        std::istringstream ss(line.substr(colon + 1));
        std::string tok;
        uint32_t a = (uint32_t)addr;
        while (ss >> tok) {
            char* be = nullptr;
            unsigned long byte = std::strtoul(tok.c_str(), &be, 16);
            if (*be != '\0' || byte > 0xFF) {
                std::fprintf(stderr, "Error: neispravan bajt u -hex zapisu: '%s'\n", tok.c_str());
                return false;
            }
            mem_[a++] = (uint8_t)byte;
        }
    }
    return true;
}

void Emulator::enterInterrupt(uint32_t cause) {
    uint32_t sp = gpr[14];
    sp -= 4; store32(sp, csr[CSR_STATUS_INDEX]);
    sp -= 4; store32(sp, gpr[15]);
    gpr[14] = sp;

    csr[CSR_CAUSE_INDEX]   = cause;
    csr[CSR_STATUS_INDEX] |= ST_I; //globalno maskiranje prekida
    gpr[15]          = csr[CSR_HANDLER_INDEX   ];
}


void Emulator::checkTerminalInput() {
    unsigned char ch;
    ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n == 1) {
        // Novi pritisak gazi eventualnu prethodnu
        termIn_      = ch;
        termPending_ = true;
    }
}

void Emulator::step() {
    uint32_t pc = gpr[15];
    uint32_t word = load32(pc);
    gpr[15] = pc + 4; 

    uint32_t oc  = (word >> 28) & 0xF;
    uint32_t mod = (word >> 24) & 0xF;
    uint32_t a   = (word >> 20) & 0xF;
    uint32_t b   = (word >> 16) & 0xF;
    uint32_t c   = (word >> 12) & 0xF;
    int32_t  D   = (int32_t)((word & 0xFFF)<<20) >> 20;

    switch (oc) {
    case 0xA: //rs-20 rd-16
        switch (mod) {
            case 0x0: setGpr(b,(gpr[b]<<D)+gpr[a]); return;
            case 0x1: setGpr(b,(gpr[b]>>D)+gpr[a]); return;
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }
        return;
    case 0x0:   // halt
        running = false;
        return;

    case 0x1:   // softverski prekid (int)
        enterInterrupt(CAUSE_SOFTWARE);
        return;

    case 0x2:   // poziv potprograma (call)
        if (mod != 0x0 && mod != 0x1) { enterInterrupt(CAUSE_BAD_INSTR); return; }
        {
            uint32_t sp = gpr[14] - 4;
            store32(sp, gpr[15]);       // push povratne adrese
            gpr[14] = sp;
            uint32_t target = gpr[a] + gpr[b] + (uint32_t)D;
            gpr[15] = (mod == 0x1) ? load32(target) : target;
        }
        return;

    case 0x3:   // skok (jmp / uslovni)
    {
        bool indir = (mod & 0x8) != 0;
        bool take;
        switch (mod & 0x7) {
            case 0x0: take = true;                                          break;
            case 0x1: take = (gpr[b] == gpr[c]);                            break;
            case 0x2: take = (gpr[b] != gpr[c]);                            break;
            case 0x3: take = ((int32_t)gpr[b] > (int32_t)gpr[c]);           break;
            default:  enterInterrupt(CAUSE_BAD_INSTR);                      return;
        }
        if (take) {
            uint32_t addr = gpr[a] + (uint32_t)D;
            gpr[15] = indir ? load32(addr) : addr;
        }
        return;
    }

    case 0x4:   // atomična zamena (xchg)
        if (mod != 0x0) { enterInterrupt(CAUSE_BAD_INSTR); return; }
        {
            uint32_t tmp = gpr[b];
            setGpr(b, gpr[c]);
            setGpr(c, tmp);
        }
        return;

    case 0x5:   // aritmetičke operacije
        switch (mod) {
            case 0x0: setGpr(a, gpr[b] + gpr[c]); return;
            case 0x1: setGpr(a, gpr[b] - gpr[c]); return;
            case 0x2: setGpr(a, gpr[b] * gpr[c]); return;
            case 0x3: {
                int32_t bb = (int32_t)gpr[b], cc = (int32_t)gpr[c];
                if (cc == 0) { enterInterrupt(CAUSE_BAD_INSTR); return; }   // deljenje nulom
                uint32_t res = (bb == INT32_MIN && cc == -1) ? (uint32_t)INT32_MIN : (uint32_t)(bb / cc);
                setGpr(a, res);
                return;
            }
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    case 0x6:   // logičke operacije
        switch (mod) {
            case 0x0: setGpr(a, ~gpr[b]);          return;
            case 0x1: setGpr(a, gpr[b] & gpr[c]);  return;
            case 0x2: setGpr(a, gpr[b] | gpr[c]);  return;
            case 0x3: setGpr(a, gpr[b] ^ gpr[c]);  return;
            default:  enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    case 0x7:   // pomeračke operacije
    {
        uint32_t sh = gpr[c];
        switch (mod) {
            case 0x0: setGpr(a, sh >= 32 ? 0 : (gpr[b] << sh)); return;
            case 0x1: setGpr(a, sh >= 32 ? 0 : (gpr[b] >> sh)); return;
            default:  enterInterrupt(CAUSE_BAD_INSTR);          return;
        }
    }

    case 0x8:   // smeštanje podatka (store)
        switch (mod) {
            case 0x0: store32(gpr[a] + gpr[b] + (uint32_t)D, gpr[c]); return;
            case 0x2: store32(load32(gpr[a] + gpr[b] + (uint32_t)D), gpr[c]); return;
            case 0x1: {   // gpr[A]<=gpr[A]+D; mem32[gpr[A]]<=gpr[C]  (push)
                setGpr(a, gpr[a] + (uint32_t)D);
                store32(gpr[a], gpr[c]);
                return;
            }
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    case 0x9:   // učitavanje podatka (load) + rad sa csr
        switch (mod) {
            case 0x0:   // gpr[A]<=csr[B]
                if (b > 2) { enterInterrupt(CAUSE_BAD_INSTR); return; }
                setGpr(a, csr[b]);
                return;
            case 0x1:   // gpr[A]<=gpr[B]+D
                setGpr(a, gpr[b] + (uint32_t)D);
                return;
            case 0x2:   // gpr[A]<=mem32[gpr[B]+gpr[C]+D]
                setGpr(a, load32(gpr[b] + gpr[c] + (uint32_t)D));
                return;
            case 0x3: // gpr[A]<=mem32[gpr[B]]; gpr[B]<=gpr[B]+D  (post-inkrement)
                setGpr(a, load32(gpr[b]));
                setGpr(b, gpr[b] + (uint32_t)D);
                return;
            case 0x4:   // csr[A]<=gpr[B]
                if (a > 2) { enterInterrupt(CAUSE_BAD_INSTR); return; }
                csr[a] = gpr[b];
                return;
            case 0x5:   // csr[A]<=csr[B]|D
                if (a > 2 || b > 2) { enterInterrupt(CAUSE_BAD_INSTR); return; }
                csr[a] = csr[b] | (uint32_t)D;
                return;
            case 0x6:   // csr[A]<=mem32[gpr[B]+gpr[C]+D]
                if (a > 2) { enterInterrupt(CAUSE_BAD_INSTR); return; }
                csr[a] = load32(gpr[b] + gpr[c] + (uint32_t)D);
                return;
            case 0x7: { // csr[A]<=mem32[gpr[B]]; gpr[B]<=gpr[B]+D
                if (a > 2) { enterInterrupt(CAUSE_BAD_INSTR); return; }
                uint32_t v = load32(gpr[b]);
                csr[a] = v;
                setGpr(b, gpr[b] + (uint32_t)D);
                return;
            }
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    default:    // nepostojeći operacioni kod
        enterInterrupt(CAUSE_BAD_INSTR);
        return;
    }
}

void Emulator::printProcessorState() const {
    std::printf("-----------------------------------------------------------------\n");
    std::printf("Emulated processor executed halt instruction\n");
    std::printf("Emulated processor state:\n");
    for (int i = 0; i < 16; i++) {
        std::printf("%s%sr%d=0x%08x",
                    (i % 4 == 0 ? "" : "   "),   
                    (i < 10 ? " " : ""),      
                    i, gpr[i]);
        if (i % 4 == 3) std::printf("\n");
    }
    std::fflush(stdout);
}

void Emulator::termInit() {
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &savedTermios_) == 0) {
        termios raw = savedTermios_;
        raw.c_lflag &= ~(ICANON | ECHO);   // bez linijskog baferisanja i bez eha
        raw.c_cc[VMIN]  = 0;               // read() ne čeka: 0 bajtova ako nema tastera
        raw.c_cc[VTIME] = 0;               // i bez tajmauta
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) termiosChanged_ = true;
    }
}

void Emulator::termRestore() {
    if (termiosChanged_) {
        tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios_);
        termiosChanged_ = false;
    }
}

int Emulator::run(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "Upotreba: emulator <naziv_ulazne_datoteke>\n");
        return 1;
    }
    if (!loadHex(argv[1])) return 1;

    gpr[15] = RESET_PC;   // pc na adresu prve instrukcije nakon reseta
    termInit();

    while (running) {
        checkTerminalInput();
        if (termPending_ && (csr[CSR_STATUS_INDEX] & ST_I) == 0
                         && (csr[CSR_STATUS_INDEX] & ST_TL) == 0) {
            termPending_ = false;
            enterInterrupt(CAUSE_TERMINAL);
        }

        step();
    }

    termRestore();
    printProcessorState();
    return 0;
}

int main(int argc, char* argv[]) {
    Emulator emu;
    return emu.run(argc, argv);
}
