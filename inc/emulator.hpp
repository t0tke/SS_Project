#pragma once
// emulator.hpp – SS 2025/2026
// Interpretativni emulator apstraktnog računarskog sistema (nivo B).
//
// Emulira 32-bitni dvoadresni procesor (Von-Neumann, little-endian, bajt
// adresibilan, adresni prostor 2^32 B) opisan u prilogu postavke, uključujući
// mehanizam prekida i periferiju terminal. Tajmer (nivo C) NIJE podržan.
//
// Ulaz je -hex izlaz linkera: tekstualni parovi (adresa, sadržaj) oblika
//   AAAAAAAA: BB BB BB BB BB BB BB BB
// Procesor nakon reseta počinje izvršavanje od adrese 0x40000000 i radi dok
// se ne izvrši halt instrukcija, nakon čega se ispisuje stanje procesora.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <termios.h>

class Emulator {
public:
    int run(int argc, char* argv[]);

private:
    // ---- Stanje procesora ----
    uint32_t gpr[16] = {0};   // r0..r15 (r0 ožičen na 0, r14=sp, r15=pc)
    uint32_t csr[3]  = {0};   // [0]=status, [1]=handler, [2]=cause
    bool     running = true;

    // ---- Memorija: retka, bajt-adresibilna (2^32 B se ne alocira ceo) ----
    std::unordered_map<uint32_t, uint8_t> mem_;

    // ---- Terminal ----
    uint32_t termIn_      = 0;       // poslednji pritisnut taster (term_in registar)
    bool     termPending_ = false;   // neisporučen zahtev za prekid od terminala
    bool     termiosChanged_      = false;   // da li je terminal prebačen u režim
    termios  savedTermios_{};        // originalna podešavanja terminala (za restore)

    // ---- Memorijski pristup (little-endian, sa memorijski mapiranim registrima) ----
    uint32_t load32(uint32_t addr);
    void     store32(uint32_t addr, uint32_t value);

    // ---- Tok emulacije ----
    bool loadHex(const std::string& path);
    void step();                     // izvrši jednu instrukciju
    void enterInterrupt(uint32_t cause);
    void checkTerminalInput();
    void printProcessorState() const;

    // ---- Terminal setup / restore ----
    void termInit();
    void termRestore();

    // r0 je ožičen na nulu: upis u indeks 0 se ignoriše.
    // (Definicija u telu klase je već implicitno inline.)
    void setGpr(uint32_t i, uint32_t v) { if (i != 0) gpr[i] = v; }
};
