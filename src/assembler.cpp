// assembler.cpp – SS 2025/2026
#include "assembler.hpp"
#include "objfile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>

extern int   yyparse();
extern int   yylineno;
extern FILE* yyin;
Assembler* gAssembler = nullptr;

// ===== Konstruktor =====
Assembler::Assembler(const std::string& inFile, const std::string& outFile)
    : inFile_(inFile), outFile_(outFile), curSection_(""),
      pendingInstr_(0), pendingNeedsPool_(false),
      pendingLdNeedsDeref_(false) {}

// ===== assemble() =====
int Assembler::assemble() {
    yyin = fopen(inFile_.c_str(), "r");
    if (!yyin) { fprintf(stderr,"Error: ne mogu da otvorim '%s'\n",inFile_.c_str()); return 1; }
    gAssembler = this;
    int ret = yyparse();
    fclose(yyin);
    if (ret != 0) { fprintf(stderr,"Error: parsiranje nije uspelo\n"); return 1; }
    flushCurrentPool();
    backpatch();
    writeBinaryOutput(outFile_);            // handler.o        (binarni predmetni fajl)
    writeTextOutput(outFile_ + ".txt");     // handler.o.txt    (čitljiv prikaz)
    return 0;
}

// ===== Interne metode =====
SectionInfo& Assembler::curSec() {
    if (curSection_.empty()) { fprintf(stderr,"Error (linija %d): izvan sekcije\n",yylineno); exit(1); }
    return sections_[curSection_];
}
int Assembler::lc() { return curSection_.empty() ? 0 : sections_[curSection_].lc; }

int Assembler::regIdx(const char* reg) {
    return atoi(reg + 2);
}
int Assembler::csrIdx(const char* csr) {
    if (!strcmp(csr,"status"))  return 0;
    if (!strcmp(csr,"handler")) return 1;
    if (!strcmp(csr,"cause"))   return 2;
    fprintf(stderr,"Error: nepoznat CSR '%s'\n",csr); exit(1);
}
int32_t Assembler::parseLit(const char* s) {
    return (int32_t)strtol((*s=='$') ? s+1 : s, nullptr, 0);
}
bool Assembler::isNumStr(const std::string& s) {
    if (s.empty()) return false;
    char* e; strtol(s.c_str(),&e,0); return *e=='\0';
}

void Assembler::appendLE32(SectionInfo& sec, uint32_t value) {
    sec.data.push_back((uint8_t)(value & 0xFF));
    sec.data.push_back((uint8_t)((value >> 8) & 0xFF));
    sec.data.push_back((uint8_t)((value >> 16) & 0xFF));
    sec.data.push_back((uint8_t)((value >> 24) & 0xFF));
    sec.lc += 4;
}

void Assembler::emit32(uint32_t value) {
    appendLE32(curSec(), value);
}
void Assembler::patch32(SectionInfo& sec, int off, uint32_t v) {
    sec.data[off]=(uint8_t)(v&0xFF); sec.data[off+1]=(uint8_t)((v>>8)&0xFF);
    sec.data[off+2]=(uint8_t)((v>>16)&0xFF); sec.data[off+3]=(uint8_t)((v>>24)&0xFF);
}
uint32_t Assembler::read32(const SectionInfo& sec, int off) {
    return (uint32_t)sec.data[off]|((uint32_t)sec.data[off+1]<<8)
          |((uint32_t)sec.data[off+2]<<16)|((uint32_t)sec.data[off+3]<<24);
}
void Assembler::ensureSymStub(const std::string& name) {
    if (!symtab_.count(name)) {
        Symbol s; s.type=SymbolType::UND; s.value=0; s.section="UND"; s.isGlobal=false; s.isDefined=false;
        symtab_[name]=s;
    }
}

void Assembler::addToPool(const std::string& lit, int instrPos) {
    SectionInfo& sec=curSec();
    for (int i=0;i<(int)sec.pool.size();i++) {
        if (sec.pool[i].value==lit) { sec.pool[i].instrPos.push_back(instrPos); return; }
    }
    PoolEntry e; e.value=lit; e.instrPos.push_back(instrPos);
    sec.pool.push_back(e);
}
void Assembler::flushPool(SectionInfo& sec) {
    for (auto& entry : sec.pool) {
        int slotPos=sec.lc;
        if (isNumStr(entry.value)) {
            uint32_t v = (uint32_t)(strtol(entry.value.c_str(), nullptr, 0));
            appendLE32(sec, v);
        } else {
            appendLE32(sec, 0);
            addReloc(sec,slotPos,"ABS_32",entry.value,0);
        }
        for (int pos : entry.instrPos) {
            int disp=slotPos-(pos+4);
            if (disp<-2048||disp>2047) {
                fprintf(stderr,"Error: pool pomeraj %d izvan 12b (instr@0x%X,slot@0x%X)\n",disp,pos,slotPos);
                exit(1);
            }
            uint32_t insn=read32(sec,pos);
            patch32(sec,pos,(insn&~0xFFFu)|((uint32_t)disp&0xFFFu));
        }
    }
    sec.pool.clear();
}
void Assembler::flushCurrentPool() { if (!curSection_.empty()) flushPool(sections_[curSection_]); }

void Assembler::addReloc(SectionInfo& sec, int off, const std::string& type, const std::string& sym, int addend) {
    RelEntry r; r.offset=off; r.type=type; r.symbol=sym; r.addend=addend;
    sec.relocs.push_back(r);
}

/*
PC_REL grana u backpatch je potpuno mrtav kod jer:

Branch (beq, bne, bgt) → ide kroz pool → relokacija je ABS_32
JMP/CALL → ide kroz pool → relokacija je ABS_32
.word simbol → addReloc → relokacija je ABS_32*/
void Assembler::backpatch() {
    for (auto& kv : sections_) {
        const std::string& sn=kv.first;
        SectionInfo& sec=kv.second;
        std::vector<RelEntry> keep;
        for (auto& rel : sec.relocs) {
            auto it=symtab_.find(rel.symbol);
            if (it==symtab_.end()) { keep.push_back(rel); continue; }
            Symbol sym=it->second;
            if (!sym.isGlobal&&sym.section==sn&&sym.isDefined) {
                if (rel.type=="ABS_32") {
                    uint32_t v=(uint32_t)(sym.value+rel.addend);
                    patch32(sec,rel.offset,v);
                    RelEntry r2; r2.offset=rel.offset; r2.type="ABS_32";
                    r2.symbol="."+sn; r2.addend=(int)v; keep.push_back(r2);
                }
                continue;
            }
            if (!sym.isGlobal&&sym.section!="UND"&&sym.isDefined) {
                int ad=sym.value+rel.addend;
                patch32(sec,rel.offset,(uint32_t)ad);
                RelEntry r2; r2.offset=rel.offset; r2.type="ABS_32";
                r2.symbol="."+sym.section; r2.addend=ad; keep.push_back(r2); continue;
            }
            keep.push_back(rel);
        }
        sec.relocs=keep;
    }
}

// Sastavi neutralan model predmetnog fajla iz internog stanja asemblera.
// Model je isti oblik koji čita i reader, pa je round-trip provera diff teksta.
ObjectModel Assembler::buildModel() const {
    ObjectModel m;
    m.sectionOrder = sectionOrder_;
    m.symtab       = symtab_;
    for (auto& sectionName : sectionOrder_) {
        auto it = sections_.find(sectionName);
        if (it == sections_.end()) continue;
        const SectionInfo& sec = it->second;
        ObjSectionData sd;
        sd.data = sec.data;
        for (auto& r : sec.relocs) {
            ObjReloc rr;
            rr.offset = (uint32_t)r.offset;
            rr.symbol = r.symbol;
            rr.addend = (int32_t)r.addend;   // tip se ne čuva – sve su ABS_32
            sd.relocs.push_back(rr);
        }
        m.sections[sectionName] = std::move(sd);
    }
    return m;
}

// Binarni predmetni fajl (sopstveni format "SSOB"), otvoren u binarnom modu.
void Assembler::writeBinaryOutput(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr,"Error: ne mogu '%s'\n",path.c_str()); exit(1); }
    objWriteBinary(f, buildModel());
}

// Čitljivi tekstualni prikaz istog predmetnog fajla.
void Assembler::writeTextOutput(const std::string& path) {
    std::ofstream f(path);
    if (!f) { fprintf(stderr,"Error: ne mogu '%s'\n",path.c_str()); exit(1); }
    objWriteText(f, buildModel());
}

// ===== Direktive =====
void Assembler::directiveGlobal(const char* sym) {
    std::string n(sym); // konstruktor koji konvertuje const char* u std::string!
    ensureSymStub(n); symtab_[n].isGlobal=true;
}
void Assembler::directiveExtern(const char* sym) { directiveGlobal(sym); }

void Assembler::startSection(const char* name) {
    flushCurrentPool();
    curSection_=std::string(name);
    if (!sections_.count(curSection_)) {
        SectionInfo sec; sec.lc=0; sections_[curSection_]=sec;
        Symbol s; s.type=SymbolType::SECTION; s.value=0; s.section=curSection_; s.isGlobal=false; s.isDefined=true;
        symtab_["."+curSection_]=s;
        sectionOrder_.push_back(curSection_);
    }
}
void Assembler::directiveWordLit(const char* numStr) {
    emit32((uint32_t)(parseLit(numStr)));
}
void Assembler::directiveWordSym(const char* symName) {
    std::string n(symName); ensureSymStub(n);
    int off=lc();
    emit32(0);
    addReloc(curSec(),off,"ABS_32",n,0);
}
void Assembler::directiveSkip(const char* numStr) {
    int n=(int)strtol(numStr,nullptr,0);
    if (n < 0) { fprintf(stderr,"Error: .skip ne može biti negativan\n"); exit(1); }
    SectionInfo& sec=curSec();
    for (int i=0;i<n;i++) sec.data.push_back(0);
    sec.lc+=n;
}
void Assembler::directiveAscii(const char* strTok) {
    int len=(int)strlen(strTok);
    SectionInfo& sec=curSec();
    for (int i=1;i<len-1;i++) {
        if (strTok[i]=='\\'&&i+1<len-1) {
            i++;
            switch(strTok[i]) {
                case 'n': sec.data.push_back('\n'); break; case 't': sec.data.push_back('\t'); break;
                case 'r': sec.data.push_back('\r'); break; case '0': sec.data.push_back('\0'); break;
                case '\\':sec.data.push_back('\\'); break; case '"': sec.data.push_back('"');  break;
                default:  sec.data.push_back((uint8_t)strTok[i]); break;
            }
        } else { sec.data.push_back((uint8_t)strTok[i]); }
        sec.lc++;
    }
}
void Assembler::directiveEnd() { flushCurrentPool(); }

void Assembler::defineLabel(const char* name) {
    std::string sn(name);
    if (symtab_.count(sn)&&symtab_[sn].isDefined) {
        fprintf(stderr,"Error (linija %d): '%s' već definisan\n",yylineno,name); exit(1);
    }
    if (curSection_.empty()) { fprintf( stderr, "Error (linija %d): labela '%s' definisana van sekcije\n", yylineno, name); exit(1); }
    bool wasG=symtab_.count(sn)&&symtab_[sn].isGlobal;
    Symbol s; s.type=SymbolType::LABEL; s.value=lc(); s.section=curSection_; s.isGlobal=wasG; s.isDefined=true;
    symtab_[sn]=s;
}

// ===== Instrukcije =====
void Assembler::encodeHalt() { emit32(0x00000000u); }
void Assembler::encodeInt()  { emit32(0x10000000u); }
void Assembler::encodeIret() {
    emit32(0x93FE0004u); // pop pc
    emit32(0x970E0004u); // pop status
}
void Assembler::encodeRet()  { emit32(0x93FE0004u); }

void Assembler::encodePush(const char* reg) {
    int r=regIdx(reg);
    emit32(0x81E00000u|((r&0xF)<<12)|((uint32_t)((-4)&0xFFF)));
}
void Assembler::encodePop(const char* reg) {
    int r=regIdx(reg);
    emit32(0x930E0004u|((r&0xF)<<20));
}
void Assembler::encodeNot(const char* reg) {
    int r=regIdx(reg); emit32(0x60000000u|((r&0xF)<<20)|((r&0xF)<<16));
}
void Assembler::encodeXchg(const char* rs, const char* rd) {
    emit32(0x40000000u|((regIdx(rd)&0xF)<<16)|((regIdx(rs)&0xF)<<12));
}

void Assembler::encodeAluRR(uint32_t opBase,const char* rs, const char* rd) {
    int source = regIdx(rs);
    int destination = regIdx(rd);

    emit32(
        opBase |
        ((uint32_t)(destination & 0xF) << 20) |
        ((uint32_t)(destination & 0xF) << 16) |
        ((uint32_t)(source & 0xF) << 12)
    );
}

void Assembler::encodeAdd(const char* rs, const char* rd) {
    encodeAluRR(0x50000000u, rs, rd);
}
void Assembler::encodeSub(const char* rs, const char* rd) {
    encodeAluRR(0x51000000u, rs, rd);
}
void Assembler::encodeMul(const char* rs, const char* rd) {
    encodeAluRR(0x52000000u, rs, rd);
}
void Assembler::encodeDiv(const char* rs, const char* rd) {
    encodeAluRR(0x53000000u, rs, rd);
}
void Assembler::encodeAnd(const char* rs, const char* rd) {
    encodeAluRR(0x61000000u, rs, rd);
}
void Assembler::encodeOr(const char* rs, const char* rd) {
    encodeAluRR(0x62000000u, rs, rd);
}
void Assembler::encodeXor(const char* rs, const char* rd) {
    encodeAluRR(0x63000000u, rs, rd);
}
void Assembler::encodeShl(const char* rs, const char* rd) {
    encodeAluRR(0x70000000u, rs, rd);
}
void Assembler::encodeShr(const char* rs, const char* rd) {
    encodeAluRR(0x71000000u, rs, rd);
}

// JMP/CALL: emit pool-instrukciju pa flush patch-uje pomeraj
void Assembler::encodeJmpLit(const char* numStr) {
    if (curSection_.empty()) { fprintf(stderr,"Error: jmp izvan sekcije\n"); exit(1); }
    int pos=lc(); emit32(0x38F00000u); addToPool(std::string(numStr),pos);
}
void Assembler::encodeJmpSym(const char* symName) {
    if (curSection_.empty()) { fprintf(stderr,"Error: jmp izvan sekcije\n"); exit(1); }
    std::string n(symName); ensureSymStub(n);
    int pos=lc(); emit32(0x38F00000u); addToPool(n,pos);
}
void Assembler::switchToCall() {
    SectionInfo& sec=curSec(); int pos=sec.lc-4;
    uint32_t insn=read32(sec,pos);
    // JMP(OC=3,MOD=8,A=15) → CALL(OC=2,MOD=1,A=15), zadržavamo D
    patch32(sec,pos,(insn&0x000FFFFFu)|0x21F00000u);
}
void Assembler::patchBranch(BranchType bt, const char* reg1, const char* reg2) {
    SectionInfo& sec=curSec(); int pos=sec.lc-4;
    uint32_t insn=read32(sec,pos);
    int r1=regIdx(reg1), r2=regIdx(reg2);
    uint8_t mod=(bt==BR_BEQ)?0x9:(bt==BR_BNE)?0xA:0xB;
    // OC=3, MOD=mod, A=15, B=r1, C=r2, D=disp(iz poola)
    patch32(sec,pos,0x30000000u|((uint32_t)(mod&0xF)<<24)|(0xF<<20)|
                    ((r1&0xF)<<16)|((r2&0xF)<<12)|(insn&0xFFFu));
}

// LD operandi
void Assembler::ldImmediateOp(const char* imm) {
    pendingLdNeedsDeref_ = false;
    std::string val = std::string(imm).substr(1);
    if (isNumStr(val)) {
        int32_t v=(int32_t)strtol(val.c_str(),nullptr,0);
        if (v>=-2048&&v<=2047) {
            // OC=9,MOD=1,A=rd(patch),B=0,D=v → gpr[A]<=0+v
            pendingInstr_=0x91000000u|((uint32_t)v&0xFFFu); pendingNeedsPool_=false; return;
        }
    }
    if (!isNumStr(val)) ensureSymStub(val);
    int instrPos=lc(); emit32(0x92FF0000u); // placeholder, A=rd patch u finalizeLD
    addToPool(val,instrPos); pendingInstr_=0; pendingNeedsPool_=true;
}
void Assembler::ldMemLitOp(const char* numStr) {
    int instrPos=lc(); emit32(0x92FF0000u);
    addToPool(std::string(numStr),instrPos); pendingInstr_=0; pendingNeedsPool_=true; pendingLdNeedsDeref_ = true;
}
void Assembler::ldMemSymOp(const char* symName) {
    std::string n(symName); ensureSymStub(n);
    int instrPos=lc(); emit32(0x92FF0000u);
    addToPool(n,instrPos); pendingInstr_=0; pendingNeedsPool_=true; pendingLdNeedsDeref_ = true;
}
void Assembler::ldRegOp(const char* reg) {
    pendingInstr_=0x91000000u|((regIdx(reg)&0xF)<<16); pendingNeedsPool_=false; pendingLdNeedsDeref_ = false;
}
void Assembler::ldMemRegOp(const char* reg) {
    pendingInstr_=0x92000000u|((regIdx(reg)&0xF)<<16); pendingNeedsPool_=false; pendingLdNeedsDeref_ = false;
}
void Assembler::ldMemRegLitOp(const char* reg, const char* numStr) {
    int32_t disp=(int32_t)strtol(numStr,nullptr,0);
    if (disp<-2048||disp>2047) { fprintf(stderr,"Error: offset %d izvan 12b\n",disp); exit(1); }
    pendingInstr_=0x92000000u|((regIdx(reg)&0xF)<<16)|((uint32_t)disp&0xFFFu);
    pendingNeedsPool_=false; pendingLdNeedsDeref_ = false;
}
void Assembler::ldMemRegSymOp(const char* reg, const char* sym) {
    // Konacna vrednost simbola nije poznata u trenutku asembliranja
    // (labele su relativne u odnosu na sekciju, extern simboli nedefinisani),
    // pa je [%reg + simbol] po postavci uvek greska u procesu asembliranja.
    (void)reg;
    fprintf(stderr,"Error (linija %d): konacna vrednost simbola '%s' u [%%reg + simbol] nije poznata u trenutku asembliranja\n",yylineno, sym);
    exit(1);
}

void Assembler::finalizeLD(const char* rd) {
    int r=regIdx(rd);
    if (pendingNeedsPool_) {
        SectionInfo& sec=curSec(); int pos=sec.lc-4;
        uint32_t insn=read32(sec,pos);
        patch32(sec,pos,(insn&0xFF0FFFFFu)|((uint32_t)(r&0xF)<<20));
        if(pendingLdNeedsDeref_) emit32(0x92000000u | ((uint32_t)(r & 0xF) << 20) | ((uint32_t)(r & 0xF) << 16));
    } else {
        pendingInstr_=(pendingInstr_&0xFF0FFFFFu)|((uint32_t)(r&0xF)<<20);
        emit32(pendingInstr_);
    }
    pendingInstr_=0; pendingNeedsPool_=false; pendingLdNeedsDeref_ = false;
}

// ST operandi
void Assembler::stMemLitOp(const char* numStr) {
    int instrPos=lc(); emit32(0x82F00000u);
    addToPool(std::string(numStr),instrPos); pendingInstr_=0; pendingNeedsPool_=true;
}
void Assembler::stMemSymOp(const char* symName) {
    std::string n(symName); ensureSymStub(n);
    int instrPos=lc(); emit32(0x82F00000u);
    addToPool(n,instrPos); pendingInstr_=0; pendingNeedsPool_=true; 
}
void Assembler::stMemRegOp(const char* reg) {
    pendingInstr_=0x80000000u|((regIdx(reg)&0xF)<<20); pendingNeedsPool_=false; 
}
void Assembler::stMemRegLitOp(const char* reg, const char* numStr) {
    int32_t disp=(int32_t)strtol(numStr,nullptr,0);
    if (disp<-2048||disp>2047) { fprintf(stderr,"Error: ST offset izvan 12b\n"); exit(1); }
    pendingInstr_=0x80000000u|((regIdx(reg)&0xF)<<20)|((uint32_t)disp&0xFFFu);
    pendingNeedsPool_=false; 
}
void Assembler::stMemRegSymOp(const char* reg, const char* sym) {
    // Isto kao kod ld: konacna vrednost simbola nije poznata u trenutku
    // asembliranja, pa je [%reg + simbol] po postavci uvek greska.
    (void)reg;
    fprintf(stderr, "Error (linija %d): konacna vrednost simbola '%s' u [%%reg + simbol] nije poznata u trenutku asembliranja\n", yylineno, sym);
    exit(1);
}
void Assembler::finalizeST(const char* rs) {
    int r=regIdx(rs);
    if (pendingNeedsPool_) {
        SectionInfo& sec=curSec(); int pos=sec.lc-4;
        uint32_t insn=read32(sec,pos);
        patch32(sec,pos,(insn&0xFFFF0FFFu)|((uint32_t)(r&0xF)<<12));
    } else {
        pendingInstr_=(pendingInstr_&0xFFFF0FFFu)|((uint32_t)(r&0xF)<<12);
        emit32(pendingInstr_);
    }
    pendingInstr_=0; pendingNeedsPool_=false; 
}

void Assembler::encodeCsrrd(const char* csr, const char* gpr) {
    emit32(0x90000000u|((regIdx(gpr)&0xF)<<20)|((csrIdx(csr)&0xF)<<16));
}
void Assembler::encodeCsrwr(const char* gpr, const char* csr) {
    emit32(0x94000000u|((csrIdx(csr)&0xF)<<20)|((regIdx(gpr)&0xF)<<16));
}

// ===== main =====
int main(int argc, char* argv[]) {
    const char* outFile="izlaz.o", *inFile=nullptr;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-o")&&i+1<argc) outFile=argv[++i];
        else inFile=argv[i];
    }
    if (!inFile) { fprintf(stderr,"Upotreba: asembler [-o izlaz.o] ulaz.s\n"); return 1; }
    Assembler asmObj(inFile,outFile);
    return asmObj.assemble();
}