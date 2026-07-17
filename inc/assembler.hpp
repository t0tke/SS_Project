#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

enum BranchType { BR_BEQ = 1, BR_BNE = 2, BR_BGT = 3 }; // Usklađen enum sa .cpp
enum class SymbolType { UND, LABEL, SECTION };

inline const char* symTypeName(SymbolType t) {
    switch (t) {
        case SymbolType::LABEL:   return "LABEL";
        case SymbolType::SECTION: return "SECTION";
        default:                  return "UND";
    }
};

struct Symbol {
    SymbolType type; 
    uint32_t value;      // ofset/adresa u 32-bitnom neoznačenom adresnom prostoru
    std::string section;
    bool isGlobal;
    bool isDefined;
    Symbol() : type(SymbolType::UND), value(0), section("UND"), isGlobal(false), isDefined(false) {}
};

struct RelEntry { 
    int offset; 
    std::string type; // U tvojim metodama koristiš stringove poput "ABS_32", "PC_REL"
    std::string symbol; 
    int addend; 
};

struct PoolEntry { 
    std::string value; 
    std::vector<int> instrPos; 
};

struct SectionInfo {
    int lc;
    std::vector<uint8_t> data;
    std::vector<RelEntry> relocs;
    std::vector<PoolEntry> pool;
};

class Assembler {
public:
    Assembler(const std::string& inFile, const std::string& outFile);
    int assemble();

    // Direktive
    void directiveGlobal(const char* sym);
    void directiveExtern(const char* sym);
    void startSection(const char* name);
    void directiveWordLit(const char* numStr);
    void directiveWordSym(const char* symName);
    void directiveSkip(const char* numStr);
    void directiveAscii(const char* strTok);
    void directiveEnd();
    void defineLabel(const char* name);

    // Instrukcije
    void encodeHalt(); void encodeInt(); void encodeIret(); void encodeRet();
    void encodePush(const char* reg); void encodePop(const char* reg); void encodeNot(const char* reg);
    void encodeXchg(const char* rs, const char* rd);
    void encodeAluRR(uint32_t opBase,const char* rs,const char* rd);
    void encodeAdd(const char* rs, const char* rd); void encodeSub(const char* rs, const char* rd);
    void encodeMul(const char* rs, const char* rd); void encodeDiv(const char* rs, const char* rd);
    void encodeAnd(const char* rs, const char* rd); void encodeOr(const char* rs, const char* rd);
    void encodeXor(const char* rs, const char* rd);
    void encodeShl(const char* rs, const char* rd); void encodeShr(const char* rs, const char* rd);
    void encodeCsrrd(const char* csr, const char* gpr); void encodeCsrwr(const char* gpr, const char* csr);
    void encodeJmpLit(const char* numStr); void encodeJmpSym(const char* symName);
    void switchToCall(); 
    void patchBranch(BranchType bt, const char* reg1, const char* reg2);
    
    void ldImmediateOp(const char* imm); void ldMemLitOp(const char* numStr);
    void ldMemSymOp(const char* symName); void ldRegOp(const char* reg);
    void ldMemRegOp(const char* reg); void ldMemRegLitOp(const char* reg, const char* numStr);
    void ldMemRegSymOp(const char* reg, const char* sym); void finalizeLD(const char* rd);
    
    void stMemLitOp(const char* numStr); void stMemSymOp(const char* symName);
    void stMemRegOp(const char* reg); void stMemRegLitOp(const char* reg, const char* numStr);
    void stMemRegSymOp(const char* reg, const char* sym); void finalizeST(const char* rs);

private:
    std::string inFile_, outFile_;
    std::string curSection_;
    uint32_t pendingInstr_;
    bool pendingNeedsPool_;
    bool pendingLdNeedsDeref_;
    
    std::map<std::string, SectionInfo> sections_;
    std::vector<std::string> sectionOrder_;
    std::map<std::string, Symbol> symtab_;

    SectionInfo& curSec();
    int lc();
    int regIdx(const char* reg);
    int csrIdx(const char* csr);
    int32_t parseLit(const char* s);
    bool isNumStr(const std::string& s);
    
    void appendLE32(SectionInfo& sec, uint32_t value);
    void emit32(uint32_t w);
    void patch32(SectionInfo& sec, int off, uint32_t v);
    uint32_t read32(const SectionInfo& sec, int off);
    void ensureSymStub(const std::string& name);
    void addToPool(const std::string& lit, int instrPos);
    void flushPool(SectionInfo& sec);
    void flushCurrentPool();
    void addReloc(SectionInfo& sec, int off, const std::string& t, const std::string& sym, int addend);
    void backpatch();
    void writeOutput();
};