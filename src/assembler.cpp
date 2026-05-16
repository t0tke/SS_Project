// assembler.cpp – SS 2025/2026
#include "assembler.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iomanip>

extern int   yyparse();
extern int   yylineno;
extern FILE* yyin;
Assembler* gAssembler = nullptr;

// ===== Konstruktor =====
Assembler::Assembler(const std::string& inFile, const std::string& outFile)
    : inFile_(inFile), outFile_(outFile), curSection_(""),
      pendingInstr_(0), pendingNeedsPool_(false) {}

// ===== assemble() =====
int Assembler::assemble() {
    yyin = fopen(inFile_.c_str(), "r");
    if (!yyin) { fprintf(stderr,"Error: ne mogu da otvorim '%s'\n",inFile_.c_str()); return 1; }
    gAssembler = this;
    int ret = yyparse();
    fclose(yyin);
    if (ret != 0) { fprintf(stderr,"Error: parsiranje nije uspelo\n"); return 1; }
    for (auto& kv : sections_) flushPool(kv.second);
    resolveEqus();
    backpatch();
    writeOutput();
    return 0;
}

// ===== Interne metode =====
SectionInfo& Assembler::curSec() {
    if (curSection_.empty()) { fprintf(stderr,"Error (linija %d): izvan sekcije\n",yylineno); exit(1); }
    return sections_[curSection_];
}
int Assembler::lc() { return curSection_.empty() ? 0 : sections_[curSection_].lc; }

int Assembler::regIdx(const char* reg) {
    const char* p = (*reg=='%') ? reg+1 : reg;
    if (p[0]=='r') return atoi(p+1);
    if (!strcmp(p,"sp")) return 14;
    if (!strcmp(p,"pc")) return 15;
    return 0;
}
int Assembler::csrIdx(const char* csr) {
    const char* p = (*csr=='%') ? csr+1 : csr;
    if (!strcmp(p,"status"))  return 0;
    if (!strcmp(p,"handler")) return 1;
    if (!strcmp(p,"cause"))   return 2;
    fprintf(stderr,"Error: nepoznat CSR '%s'\n",csr); exit(1);
}
int32_t Assembler::parseLit(const char* s) {
    return (int32_t)strtol((*s=='$') ? s+1 : s, nullptr, 0);
}
bool Assembler::isNumStr(const std::string& s) {
    if (s.empty()) return false;
    char* e; strtol(s.c_str(),&e,0); return *e=='\0';
}

void Assembler::emit32(uint32_t w) {
    SectionInfo& sec=curSec();
    sec.data.push_back(w&0xFF); sec.data.push_back((w>>8)&0xFF);
    sec.data.push_back((w>>16)&0xFF); sec.data.push_back((w>>24)&0xFF);
    sec.lc+=4;
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
        Symbol s; s.type="UND"; s.value=0; s.section="UND"; s.isGlobal=false; s.isDefined=false;
        symtab_[name]=s;
    }
}

int Assembler::addToPool(const std::string& lit, int instrPos) {
    SectionInfo& sec=curSec();
    for (int i=0;i<(int)sec.pool.size();i++) {
        if (sec.pool[i].value==lit) { sec.pool[i].instrPos.push_back(instrPos); return i; }
    }
    PoolEntry e; e.value=lit; e.instrPos.push_back(instrPos);
    sec.pool.push_back(e); return (int)sec.pool.size()-1;
}
void Assembler::flushPool(SectionInfo& sec) {
    for (auto& entry : sec.pool) {
        int slotPos=sec.lc;
        if (isNumStr(entry.value)) {
            uint32_t v=(uint32_t)strtol(entry.value.c_str(),nullptr,0);
            sec.data.push_back(v&0xFF); sec.data.push_back((v>>8)&0xFF);
            sec.data.push_back((v>>16)&0xFF); sec.data.push_back((v>>24)&0xFF);
        } else {
            auto it=symtab_.find(entry.value);
            if (it!=symtab_.end()&&it->second.type=="CONST"&&it->second.section=="ABS") {
                uint32_t v=(uint32_t)it->second.value;
                sec.data.push_back(v&0xFF); sec.data.push_back((v>>8)&0xFF);
                sec.data.push_back((v>>16)&0xFF); sec.data.push_back((v>>24)&0xFF);
            } else {
                sec.data.push_back(0); sec.data.push_back(0);
                sec.data.push_back(0); sec.data.push_back(0);
                RelEntry r; r.offset=slotPos; r.type="ABS_32"; r.symbol=entry.value; r.addend=0;
                sec.relocs.push_back(r);
            }
        }
        sec.lc+=4;
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

void Assembler::addReloc(int off, const std::string& t, const std::string& sym, int addend) {
    RelEntry r; r.offset=off; r.type=t; r.symbol=sym; r.addend=addend;
    curSec().relocs.push_back(r);
}

void Assembler::resolveEqus() {
    bool progress=true;
    while (progress&&!pendingEqus_.empty()) {
        progress=false;
        std::vector<PendingEqu> rem;
        for (auto& eq : pendingEqus_) {
            auto it1=symtab_.find(eq.src1);
            if (it1==symtab_.end()||!it1->second.isDefined) { rem.push_back(eq); continue; }
            Symbol s1=it1->second;
            if (eq.src2.empty()) {
                bool wasG=symtab_.count(eq.dest)&&symtab_[eq.dest].isGlobal;
                Symbol& d=symtab_[eq.dest];
                if (s1.type=="CONST"&&s1.section=="ABS") {
                    d.type="CONST"; d.value=s1.value+eq.addend; d.section="ABS";
                    d.isGlobal=wasG||s1.isGlobal; d.isDefined=true;
                } else if (s1.section!="UND") {
                    d.type="LABEL"; d.value=s1.value+eq.addend; d.section=s1.section;
                    d.isGlobal=wasG||s1.isGlobal; d.isDefined=true;
                } else { rem.push_back(eq); continue; }
            } else {
                auto it2=symtab_.find(eq.src2);
                if (it2==symtab_.end()||!it2->second.isDefined) { rem.push_back(eq); continue; }
                Symbol s2=it2->second;
                if (s1.section!=s2.section) {
                    fprintf(stderr,"Error: .equ %s=%s-%s: različite sekcije\n",
                            eq.dest.c_str(),eq.src1.c_str(),eq.src2.c_str()); exit(1);
                }
                bool wasG=symtab_.count(eq.dest)&&symtab_[eq.dest].isGlobal;
                Symbol& d=symtab_[eq.dest];
                d.type="CONST"; d.value=s1.value-s2.value+eq.addend;
                d.section="ABS"; d.isGlobal=wasG; d.isDefined=true;
            }
            progress=true;
        }
        pendingEqus_=rem;
    }
    for (auto& eq : pendingEqus_)
        fprintf(stderr,"Error: .equ '%s' nije razrešen\n",eq.dest.c_str());
    if (!pendingEqus_.empty()) exit(1);
}

void Assembler::backpatch() {
    for (auto& kv : sections_) {
        const std::string& sn=kv.first;
        SectionInfo& sec=kv.second;
        std::vector<RelEntry> keep;
        for (auto& rel : sec.relocs) {
            auto it=symtab_.find(rel.symbol);
            if (it==symtab_.end()) { keep.push_back(rel); continue; }
            Symbol sym=it->second;
            if (sym.type=="CONST"&&sym.section=="ABS") {
                if (rel.type=="ABS_32") patch32(sec,rel.offset,(uint32_t)(sym.value+rel.addend));
                else { fprintf(stderr,"Error: PC_REL nad ABS '%s'\n",rel.symbol.c_str()); exit(1); }
                continue;
            }
            if (!sym.isGlobal&&sym.section==sn&&sym.isDefined) {
                if (rel.type=="ABS_32") {
                    uint32_t v=(uint32_t)(sym.value+rel.addend);
                    patch32(sec,rel.offset,v);
                    RelEntry r2; r2.offset=rel.offset; r2.type="ABS_32";
                    r2.symbol="."+sn; r2.addend=(int)v; keep.push_back(r2);
                } else if (rel.type=="PC_REL") {
                    int disp=sym.value-(rel.offset+4)+rel.addend;
                    if (disp<-2048||disp>2047) {
                        fprintf(stderr,"Error: PC_REL %d za '%s' izvan 12b\n",disp,rel.symbol.c_str()); exit(1);
                    }
                    uint32_t insn=read32(sec,rel.offset);
                    patch32(sec,rel.offset,(insn&~0xFFFu)|((uint32_t)disp&0xFFFu));
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

void Assembler::writeOutput() {
    std::ofstream f(outFile_);
    if (!f) { fprintf(stderr,"Error: ne mogu '%s'\n",outFile_.c_str()); exit(1); }

    f<<"#SYMTAB\n"<<std::left<<std::setw(20)<<"Name"<<std::setw(10)<<"Type"
     <<std::setw(14)<<"Value"<<std::setw(16)<<"Section"<<"Global\n"<<std::string(63,'-')<<"\n";
    for (auto& kv : sections_) {
        std::string key="."+kv.first;
        if (!symtab_.count(key)) continue;
        Symbol& sym=symtab_[key];
        f<<std::left<<std::setw(20)<<key<<std::setw(10)<<"SECTION"<<"0x"
         <<std::hex<<std::internal<<std::setw(8)<<std::setfill('0')<<sym.value
         <<std::left<<std::setfill(' ')<<"  "<<std::setw(16)<<kv.first<<(sym.isGlobal?"yes":"no")<<"\n";
    }
    for (auto& kv : symtab_) {
        if (!kv.first.empty()&&kv.first[0]=='.') continue;
        Symbol& sym=kv.second;
        f<<std::left<<std::setw(20)<<kv.first<<std::setw(10)<<sym.type<<"0x"
         <<std::hex<<std::internal<<std::setw(8)<<std::setfill('0')<<sym.value
         <<std::left<<std::setfill(' ')<<"  "<<std::setw(16)<<sym.section<<(sym.isGlobal?"yes":"no")<<"\n";
    }
    f<<"\n";
    for (auto& kv : sections_) {
        SectionInfo& sec=kv.second;
        f<<"#SECTION "<<kv.first<<" size="<<std::dec<<sec.data.size()<<"\n";
        for (size_t i=0;i<sec.data.size();i++) {
            if (i % 16 == 0) {
                if (i) f << "\n";
                // Eksplicitno prekidamo std::left i uvodimo std::right za ispis adrese
                f << std::hex << std::right << std::setw(4) << std::setfill('0') << i << ": ";
            }
            // Sada se bajtovi garantovano ispisuju cisto
            f<<std::hex<<std::setw(2)<<std::setfill('0')<<(unsigned)sec.data[i]<<" ";
        }
        if (!sec.data.empty()) f<<"\n";
        
        // --- POPRAVLJEN DEO ZA ISPIS RELOKACIJA ---
        if (!sec.relocs.empty()) {
            f << "#RELA " << kv.first << "\n";
            
            // Eksplicitno vracamo prazan prostor (' ') kao fill karakter pre ispisa zaglavlja
            f << std::left << std::setfill(' ') 
              << std::setw(12) << "Offset" 
              << std::setw(10) << "Type"
              << std::setw(22) << "Symbol" 
              << "Addend\n" 
              << std::string(52, '-') << "\n";

            for (auto& r : sec.relocs) {
                // Prvo ispisujemo prefiks i formatiramo sam broj da ima tacno 8 heksadecimalnih cifara sa nulama na pocetku
                f << "0x" << std::hex << std::internal << std::setw(8) << std::setfill('0') << r.offset;
                
                // Vracamo levo poravnanje i razmak za preostale kolone (Type, Symbol, Addend)
                f << std::left << std::setfill(' ') 
                  << "  " << std::setw(10) << r.type 
                  << std::setw(22) << r.symbol 
                  << std::dec << r.addend << "\n";
            }
        }
        f<<"\n";
    }
}

// ===== Direktive =====
void Assembler::directiveGlobal(const char* sym) {
    std::string n(sym); ensureSymStub(n); symtab_[n].isGlobal=true;
}
void Assembler::directiveExtern(const char* sym) { directiveGlobal(sym); }

void Assembler::startSection(const char* name) {
    flushCurrentPool();
    curSection_=std::string(name);
    if (!sections_.count(curSection_)) {
        SectionInfo sec; sec.name=curSection_; sec.lc=0; sections_[curSection_]=sec;
        Symbol s; s.type="SECTION"; s.value=0; s.section=curSection_; s.isGlobal=true; s.isDefined=true;
        symtab_["."+curSection_]=s;
    }
}
void Assembler::directiveWordLit(const char* numStr) {
    int32_t v=parseLit(numStr);
    SectionInfo& sec=curSec();
    sec.data.push_back(v&0xFF); sec.data.push_back((v>>8)&0xFF);
    sec.data.push_back((v>>16)&0xFF); sec.data.push_back((v>>24)&0xFF);
    sec.lc+=4;
}
void Assembler::directiveWordSym(const char* symName) {
    std::string n(symName); ensureSymStub(n);
    SectionInfo& sec=curSec(); int off=sec.lc;
    sec.data.push_back(0); sec.data.push_back(0); sec.data.push_back(0); sec.data.push_back(0);
    addReloc(off,"ABS_32",n,0); sec.lc+=4;
}
void Assembler::directiveSkip(const char* numStr) {
    int n=(int)strtol(numStr,nullptr,0);
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
void Assembler::directiveEquExpr(const char* dest, const char* expr) {
    std::string d(dest), e(expr);
    char op=0; size_t opPos=std::string::npos;
    for (size_t i=1;i<e.size();i++) if (e[i]=='+'||e[i]=='-') { opPos=i; op=e[i]; break; }
    std::string lhs=(opPos==std::string::npos)?e:e.substr(0,opPos);
    std::string rhs=(opPos==std::string::npos)?"":e.substr(opPos+1);
    bool isSub=(op=='-');
    bool lhsNum=isNumStr(lhs), rhsNum=rhs.empty()||isNumStr(rhs);
    bool rhsSym=!rhs.empty()&&!rhsNum, lhsSym=!lhsNum&&!lhs.empty();
    bool wasG=symtab_.count(d)&&symtab_[d].isGlobal;

    auto mkstub=[&](const std::string& nm){ ensureSymStub(nm); };
    auto setConst=[&](int val) {
        Symbol s; s.type="CONST"; s.value=val; s.section="ABS"; s.isGlobal=wasG; s.isDefined=true;
        symtab_[d]=s;
    };
    auto setAlias=[&](Symbol& src, int addend) {
        Symbol s;
        if (src.type=="CONST"&&src.section=="ABS") { s.type="CONST"; s.value=src.value+addend; s.section="ABS"; }
        else { s.type="LABEL"; s.value=src.value+addend; s.section=src.section; }
        s.isGlobal=wasG||src.isGlobal; s.isDefined=true; symtab_[d]=s;
    };

    if (lhsNum&&(rhs.empty()||rhsNum)) {
        int32_t v=(int32_t)strtol(lhs.c_str(),nullptr,0);
        int32_t v2=rhs.empty()?0:(int32_t)strtol(rhs.c_str(),nullptr,0);
        setConst(isSub?(v-v2):(v+v2)); return;
    }
    if (lhsSym&&(rhs.empty()||rhsNum)) {
        int addend=rhs.empty()?0:(int)strtol(rhs.c_str(),nullptr,0); if (isSub) addend=-addend;
        auto it=symtab_.find(lhs);
        if (it!=symtab_.end()&&it->second.isDefined) { setAlias(it->second,addend); return; }
        mkstub(d); mkstub(lhs);
        PendingEqu eq; eq.dest=d; eq.src1=lhs; eq.src2=""; eq.addend=addend; eq.isSub=false;
        pendingEqus_.push_back(eq); return;
    }
    if (lhsNum&&rhsSym) {
        int addend=(int)strtol(lhs.c_str(),nullptr,0);
        auto it=symtab_.find(rhs);
        if (it!=symtab_.end()&&it->second.isDefined) { setAlias(it->second,addend); return; }
        mkstub(d); mkstub(rhs);
        PendingEqu eq; eq.dest=d; eq.src1=rhs; eq.src2=""; eq.addend=addend; eq.isSub=false;
        pendingEqus_.push_back(eq); return;
    }
    if (lhsSym&&rhsSym&&isSub) {
        auto it1=symtab_.find(lhs), it2=symtab_.find(rhs);
        if (it1!=symtab_.end()&&it1->second.isDefined&&it2!=symtab_.end()&&it2->second.isDefined) {
            if (it1->second.section!=it2->second.section) {
                fprintf(stderr,"Error: .equ %s=%s-%s: različite sekcije\n",d.c_str(),lhs.c_str(),rhs.c_str()); exit(1);
            }
            setConst(it1->second.value-it2->second.value); return;
        }
        mkstub(d); mkstub(lhs); mkstub(rhs);
        PendingEqu eq; eq.dest=d; eq.src1=lhs; eq.src2=rhs; eq.addend=0; eq.isSub=true;
        pendingEqus_.push_back(eq); return;
    }
    fprintf(stderr,"Error: .equ %s – nepodržan izraz '%s'\n",dest,expr); exit(1);
}
void Assembler::directiveEnd() { flushCurrentPool(); }

void Assembler::defineLabel(const char* name) {
    std::string sn(name);
    if (symtab_.count(sn)&&symtab_[sn].isDefined) {
        fprintf(stderr,"Error (linija %d): '%s' već definisan\n",yylineno,name); exit(1);
    }
    if (curSection_.empty()) { ensureSymStub(sn); return; }
    bool wasG=symtab_.count(sn)&&symtab_[sn].isGlobal;
    Symbol s; s.type="LABEL"; s.value=lc(); s.section=curSection_; s.isGlobal=wasG; s.isDefined=true;
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
    emit32(0x93000000u|((r&0xF)<<20)|(14<<16)|4);
}
void Assembler::encodeNot(const char* reg) {
    int r=regIdx(reg); emit32(0x60000000u|((r&0xF)<<20)|((r&0xF)<<16));
}
void Assembler::encodeXchg(const char* rs, const char* rd) {
    emit32(0x40000000u|((regIdx(rd)&0xF)<<16)|((regIdx(rs)&0xF)<<12));
}
void Assembler::encodeAdd(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x50000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeSub(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x51000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeMul(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x52000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeDiv(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x53000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeAnd(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x61000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeOr (const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x62000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeXor(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x63000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeShl(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x70000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
}
void Assembler::encodeShr(const char* rs, const char* rd) {
    int s=regIdx(rs),d=regIdx(rd); emit32(0x71000000u|((d&0xF)<<20)|((d&0xF)<<16)|((s&0xF)<<12));
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
    std::string val(imm); if (!val.empty()&&val[0]=='$') val=val.substr(1);
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
    addToPool(std::string(numStr),instrPos); pendingInstr_=0; pendingNeedsPool_=true;
}
void Assembler::ldMemSymOp(const char* symName) {
    std::string n(symName); ensureSymStub(n);
    int instrPos=lc(); emit32(0x92FF0000u);
    addToPool(n,instrPos); pendingInstr_=0; pendingNeedsPool_=true;
}
void Assembler::ldRegOp(const char* reg) {
    pendingInstr_=0x91000000u|((regIdx(reg)&0xF)<<16); pendingNeedsPool_=false;
}
void Assembler::ldMemRegOp(const char* reg) {
    pendingInstr_=0x92000000u|((regIdx(reg)&0xF)<<16); pendingNeedsPool_=false;
}
void Assembler::ldMemRegLitOp(const char* reg, const char* numStr) {
    int32_t disp=(int32_t)strtol(numStr,nullptr,0);
    if (disp<-2048||disp>2047) { fprintf(stderr,"Error: offset %d izvan 12b\n",disp); exit(1); }
    pendingInstr_=0x92000000u|((regIdx(reg)&0xF)<<16)|((uint32_t)disp&0xFFFu);
    pendingNeedsPool_=false;
}
void Assembler::ldMemRegSymOp(const char* reg, const char* sym) {
    fprintf(stderr,"Error: [%s+%s] – mora stati u 12b\n",reg,sym); exit(1);
}
void Assembler::finalizeLD(const char* rd) {
    int r=regIdx(rd);
    if (pendingNeedsPool_) {
        SectionInfo& sec=curSec(); int pos=sec.lc-4;
        uint32_t insn=read32(sec,pos);
        patch32(sec,pos,(insn&0xFF0FFFFFu)|((uint32_t)(r&0xF)<<20));
    } else {
        pendingInstr_=(pendingInstr_&0xFF0FFFFFu)|((uint32_t)(r&0xF)<<20);
        emit32(pendingInstr_);
    }
    pendingInstr_=0; pendingNeedsPool_=false;
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
    fprintf(stderr,"Error: [%s+%s] – mora stati u 12b\n",reg,sym); exit(1);
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