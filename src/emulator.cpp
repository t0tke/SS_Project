// emulator.cpp – SS 2025/2026 – Interpretativni emulator (nivo B)
//
// Podržava sve zahteve nivoa A (ceo procesor i mehanizam prekida) i periferiju
// terminal iz nivoa B. Tajmer (tim_cfg) NIJE emuliran – upisi u njegov memorijski
// mapirani registar se prihvataju kao običan pristup memoriji, ali ne generišu
// prekid.
//
// Format instrukcije (32 bita, u memoriji little-endian kao i svaki podatak):
//   OC[31:28] MOD[27:24] RegA[23:20] RegB[19:16] RegC[15:12] Disp[11:0]
// Ovaj raspored je identičan onome koji generiše asembler (emit32 + appendLE32),
// pa emulator instrukciju čita istom little-endian 32-bitnom operacijom.

#include "emulator.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <fcntl.h>

// ---- Konstante sistema ----
namespace {
    constexpr uint32_t RESET_PC   = 0x40000000u;   // adresa prve instrukcije nakon reseta
    constexpr uint32_t TERM_OUT   = 0xFFFFFF00u;    // registar izlaznih podataka terminala
    constexpr uint32_t TERM_IN    = 0xFFFFFF04u;    // registar ulaznih podataka terminala

    // Indeksi kontrolnih/statusnih registara (csr_).
    constexpr int CSR_STATUS  = 0;
    constexpr int CSR_HANDLER = 1;
    constexpr int CSR_CAUSE   = 2;

    // Raspored flegova statusne reči prema dijagramu postavke: bit0=Tr, bit1=Tl,
    // bit2=I; za svaki 0=omogućeno, 1=maskirano. Tr (tajmer) je nivo C i ovde se ne koristi.
    constexpr uint32_t ST_TL = 0x2;   // maskiranje prekida od terminala (bit 1)
    constexpr uint32_t ST_I  = 0x4;   // globalno maskiranje spoljašnjih prekida (bit 2)

    // Uzroci prekida (cause registar).
    constexpr uint32_t CAUSE_BAD_INSTR = 1;   // nekorektna instrukcija
    constexpr uint32_t CAUSE_TERMINAL  = 3;   // zahtev za prekid od terminala
    constexpr uint32_t CAUSE_SOFTWARE  = 4;   // softverski prekid (int)

    // Znakovno proširenje 12-bitnog pomeraja na 32-bitni označeni broj.
    inline int32_t signExt12(uint32_t d) {
        return (int32_t)((d & 0xFFFu) ^ 0x800u) - 0x800;
    }
}

// ======================================================================
//  Memorijski pristup (little-endian) + memorijski mapirani registri
// ======================================================================
uint32_t Emulator::load32(uint32_t addr) {
    // Sav pristup memoriji od strane instrukcija je 32-bitni, pa memorijski
    // mapirane registre presrećemo na nivou 32-bitne reči.
    if (addr == TERM_IN)  return termIn_;
    if (addr == TERM_OUT) return 0;   // čitanje izlaznog registra nema definisan sadržaj

    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        auto it = mem_.find(addr + i);
        if (it != mem_.end()) v |= (uint32_t)it->second << (8 * i);
    }
    return v;
}

void Emulator::store32(uint32_t addr, uint32_t value) {
    if (addr == TERM_OUT) {
        // Terminal ispisuje znak čiji je ASCII kod niži bajt upisane vrednosti.
        unsigned char ch = (unsigned char)(value & 0xFF);
        (void)::write(STDOUT_FILENO, &ch, 1);
        return;
    }
    if (addr == TERM_IN) {
        termIn_ = value;   // upis u ulazni registar nema efekat na terminal
        return;
    }
    for (int i = 0; i < 4; i++)
        mem_[addr + i] = (uint8_t)((value >> (8 * i)) & 0xFF);
}

// ======================================================================
//  Učitavanje -hex ulazne datoteke
// ======================================================================
bool Emulator::loadHex(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "Error: ne mogu da otvorim ulaznu datoteku '%s'\n", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;   // prazan/nevalidan red se preskače

        std::string addrStr = line.substr(0, colon);   // lokalni string: c_str()/endp ostaju validni
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

// ======================================================================
//  Ulazak u prekidnu rutinu
//  Na stek se stavljaju statusna reč pa povratna adresa (tim redom), postavlja
//  se uzrok prekida i skače na handler.
// ======================================================================
void Emulator::enterInterrupt(uint32_t cause) {
    uint32_t sp = gpr[14];
    sp -= 4; store32(sp, csr[CSR_STATUS]);
    sp -= 4; store32(sp, gpr[15]);
    gpr[14] = sp;

    csr[CSR_CAUSE]   = cause;
    csr[CSR_STATUS] |= ST_I;           // globalno maskiranje prekida (postavka)
    gpr[15]          = csr[CSR_HANDLER];
}

// ======================================================================
//  Terminal: očitavanje tastature (bez baferisanja, neblokirajuće)
// ======================================================================
void Emulator::pollTerminal() {
    unsigned char ch;
    ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n == 1) {
        // Novi pritisak gazi eventualnu prethodnu, još nepročitanu vrednost.
        termIn_      = ch;
        termPending_ = true;
    }
}

// ======================================================================
//  Izvršavanje jedne instrukcije
// ======================================================================
void Emulator::step() {
    uint32_t pc = gpr[15];
    uint32_t word = load32(pc);
    gpr[15] = pc + 4;             // podrazumevani sledeći pc (skokovi ga menjaju)

    uint32_t oc  = (word >> 28) & 0xF;
    uint32_t mod = (word >> 24) & 0xF;
    uint32_t a   = (word >> 20) & 0xF;
    uint32_t b   = (word >> 16) & 0xF;
    uint32_t c   = (word >> 12) & 0xF;
    int32_t  D   = signExt12(word & 0xFFF);

    switch (oc) {
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
            // Odredište (i eventualno indirektno čitanje) računamo tek kada se skok
            // zaista izvršava.
            uint32_t addr = gpr[a] + (uint32_t)D;
            gpr[15] = indir ? load32(addr) : addr;
        }
        return;
    }

    case 0x4:   // atomična zamena (xchg)
        if (mod != 0x0) { enterInterrupt(CAUSE_BAD_INSTR); return; }
        {
            uint32_t tmp = gpr[b];
            setReg(b, gpr[c]);
            setReg(c, tmp);
        }
        return;

    case 0x5:   // aritmetičke operacije
        switch (mod) {
            case 0x0: setReg(a, gpr[b] + gpr[c]); return;
            case 0x1: setReg(a, gpr[b] - gpr[c]); return;
            case 0x2: setReg(a, gpr[b] * gpr[c]); return;
            case 0x3: {
                int32_t bb = (int32_t)gpr[b], cc = (int32_t)gpr[c];
                if (cc == 0) { enterInterrupt(CAUSE_BAD_INSTR); return; }   // deljenje nulom
                uint32_t res = (bb == INT32_MIN && cc == -1)
                                 ? (uint32_t)INT32_MIN : (uint32_t)(bb / cc);
                setReg(a, res);
                return;
            }
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    case 0x6:   // logičke operacije
        switch (mod) {
            case 0x0: setReg(a, ~gpr[b]);          return;
            case 0x1: setReg(a, gpr[b] & gpr[c]);  return;
            case 0x2: setReg(a, gpr[b] | gpr[c]);  return;
            case 0x3: setReg(a, gpr[b] ^ gpr[c]);  return;
            default:  enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    case 0x7:   // pomeračke operacije
    {
        uint32_t sh = gpr[c];
        switch (mod) {
            case 0x0: setReg(a, sh >= 32 ? 0 : (gpr[b] << sh)); return;
            case 0x1: setReg(a, sh >= 32 ? 0 : (gpr[b] >> sh)); return;
            default:  enterInterrupt(CAUSE_BAD_INSTR);          return;
        }
    }

    case 0x8:   // smeštanje podatka (store)
        switch (mod) {
            case 0x0: store32(gpr[a] + gpr[b] + (uint32_t)D, gpr[c]); return;
            case 0x2: store32(load32(gpr[a] + gpr[b] + (uint32_t)D), gpr[c]); return;
            case 0x1: {   // gpr[A]<=gpr[A]+D; mem32[gpr[A]]<=gpr[C]  (push)
                setReg(a, gpr[a] + (uint32_t)D);
                store32(gpr[a], gpr[c]);
                return;
            }
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    case 0x9:   // učitavanje podatka (load) + rad sa csr
        switch (mod) {
            case 0x0:   // gpr[A]<=csr[B]
                if (b > 2) { enterInterrupt(CAUSE_BAD_INSTR); return; }
                setReg(a, csr[b]);
                return;
            case 0x1:   // gpr[A]<=gpr[B]+D
                setReg(a, gpr[b] + (uint32_t)D);
                return;
            case 0x2:   // gpr[A]<=mem32[gpr[B]+gpr[C]+D]
                setReg(a, load32(gpr[b] + gpr[c] + (uint32_t)D));
                return;
            case 0x3: // gpr[A]<=mem32[gpr[B]]; gpr[B]<=gpr[B]+D  (post-inkrement)
                // Redosled iz postavke: prvo upiši A (čita staro gpr[B]), pa tek onda
                // uvećaj B. Kada je A==B, ovo daje mem32[stari gpr[B]]+D (npr. pop %sp).
                setReg(a, load32(gpr[b]));
                setReg(b, gpr[b] + (uint32_t)D);
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
                setReg(b, gpr[b] + (uint32_t)D);
                return;
            }
            default: enterInterrupt(CAUSE_BAD_INSTR); return;
        }

    default:    // nepostojeći operacioni kod
        enterInterrupt(CAUSE_BAD_INSTR);
        return;
    }
}

// ======================================================================
//  Ispis stanja procesora nakon halt instrukcije
// ======================================================================
void Emulator::dumpState() const {
    std::printf("-----------------------------------------------------------------\n");
    std::printf("Emulated processor executed halt instruction\n");
    std::printf("Emulated processor state:\n");
    for (int i = 0; i < 16; i++) {
        std::printf("%s%sr%d=0x%08x",
                    (i % 4 == 0 ? "" : "   "),   // 3 razmaka između registara u redu
                    (i < 10 ? " " : ""),         // vodeći razmak: ime desno poravnato na 3 znaka
                    i, gpr[i]);
        if (i % 4 == 3) std::printf("\n");
    }
    std::fflush(stdout);
}

// ======================================================================
//  Terminal: sirovi neblokirajući ulaz (bez eho prikaza)
// ======================================================================
void Emulator::termInit() {
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &savedTermios_) == 0) {
            termios raw = savedTermios_;
            raw.c_lflag &= ~(ICANON | ECHO);   // bez linijskog baferisanja i bez eha
            raw.c_cc[VMIN]  = 0;
            raw.c_cc[VTIME] = 0;
            // Označi terminal sirovim samo ako je postavljanje zaista uspelo.
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) ttyRaw_ = true;
        }
    }
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);   // neblokirajuće čitanje tastature
    if (fl != -1 && fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK) != -1)
        savedFl_ = fl;                          // zapamti za vraćanje samo ako je uspelo
}

void Emulator::termRestore() {
    if (ttyRaw_) {
        tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios_);
        ttyRaw_ = false;
    }
    if (savedFl_ != -1) {
        fcntl(STDIN_FILENO, F_SETFL, savedFl_);   // vrati originalne zastavice (skini O_NONBLOCK)
        savedFl_ = -1;
    }
}

// ======================================================================
//  Glavni tok
// ======================================================================
int Emulator::run(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "Upotreba: emulator <naziv_ulazne_datoteke>\n");
        return 1;
    }
    if (!loadHex(argv[1])) return 1;

    gpr[15] = RESET_PC;   // pc na adresu prve instrukcije nakon reseta
    termInit();

    while (running) {
        // 1) Očitaj tastaturu i, ako nije maskiran, opsluži prekid od terminala.
        pollTerminal();
        if (termPending_ && (csr[CSR_STATUS] & ST_I) == 0
                         && (csr[CSR_STATUS] & ST_TL) == 0) {
            termPending_ = false;
            enterInterrupt(CAUSE_TERMINAL);
        }
        // 2) Atomično izvrši narednu instrukciju.
        step();
    }

    termRestore();
    dumpState();
    return 0;
}

int main(int argc, char* argv[]) {
    Emulator emu;
    return emu.run(argc, argv);
}
