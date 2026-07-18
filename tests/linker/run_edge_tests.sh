#!/usr/bin/env bash
# Rubni / bezbednosni testovi linkera i SSOB čitača.
# Pokrivaju konkretne ispravke:
#  1) lokalan nedefinisan simbol se NE razrešava globalom iz drugog fajla
#  2) -hex prijavljuje sve nerazrešene simbole (uklj. nekorišćen .extern)
#  3) stroga validacija -place (duplikat, smeće, >32 bita)
#  4) greška pri pisanju izlaza vraća nenulti exit
#  5) objReadBinary: verzija/endian/overflow/NUL/duplikati (bez pada programa)
#  6) sekcija tačno na kraju 32-bitnog prostora je validna, preko toga greška
#  7) direct -hex  ==  (-relocatable -> -hex)
#
# Pokretanje iz korena projekta:  bash tests/linker/run_edge_tests.sh
set -u
cd "$(dirname "$0")/../.."
ASM=./assembler; LNK=./linker
D=$(mktemp -d); pass=0; fail=0
ok()  { printf "  \033[32mPASS\033[0m %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  \033[31mFAIL\033[0m %s\n" "$1"; fail=$((fail+1)); }

# expect_exit <0|nonzero> "opis" cmd...
exp_ok()  { local d="$1"; shift; if "$@" >/dev/null 2>"$D/e"; then ok "$d"; else bad "$d (exit=$?, očekivan uspeh)"; fi; }
exp_err() { local d="$1" n="$2"; shift 2; local o; o=$("$@" 2>&1 >/dev/null); local rc=$?;
  if [ $rc -eq 0 ]; then bad "$d (očekivana greška, exit=0)";
  elif echo "$o" | grep -q -- "$n"; then ok "$d"; else bad "$d (poruka ne sadrži '$n': $o)"; fi; }

for s in gdef localref unusedext tiny ov_a ov_b main handler isr_terminal isr_timer; do
  src="tests/linker/$s.s"; [ -f "$src" ] || src="tests/nivo-b/$s.s"
  $ASM -o "$D/$s.o" "$src" >/dev/null 2>&1 || { echo "ASM FAIL $s"; exit 1; }
done

echo "== 1) lokalan nedefinisan simbol =="
exp_err "local-undef se ne hvata globalom" "nedefinisan lokalan simbol" \
        $LNK -hex -o "$D/x.hex" "$D/localref.o" "$D/gdef.o"

echo "== 2) nerazrešeni simboli u -hex =="
exp_err "nekorišćen .extern prijavljen" "never_defined_anywhere" \
        $LNK -hex -o "$D/x.hex" "$D/unusedext.o"

echo "== 3) stroga -place validacija =="
exp_err "duplirani -place"       "duplirani -place"   $LNK -hex -place=s@0x1000 -place=s@0x2000 -o "$D/x.hex" "$D/tiny.o"
exp_err "-place smeće adresa"     "neispravna"        $LNK -hex -place=s@xyz -o "$D/x.hex" "$D/tiny.o"
exp_err "-place @2^32"           "32-bitni"           $LNK -hex -place=s@0x100000000 -o "$D/x.hex" "$D/tiny.o"
exp_err "-place negativna"        "neispravna"        $LNK -hex -place=s@-4 -o "$D/x.hex" "$D/tiny.o"

echo "== 4) greška pri pisanju izlaza =="
exp_err "write error -> nonzero"  "ne mogu"           $LNK -hex -o "$D/nema/x.hex" "$D/tiny.o"

echo "== 5) objReadBinary bezbednost (bez pada) =="
python3 - "$D" "$LNK" <<'PY'
import struct,os,subprocess,sys
D,LNK=sys.argv[1],sys.argv[2]
good=open(f"{D}/tiny.o","rb").read()
_,_,_,_,_,_,soff,ssz,_=struct.unpack_from("<4sHHIIIIII",good,0)
n_pass=n_fail=0
def chk(name,data,expect_reject=True):
    global n_pass,n_fail
    p=f"{D}/{name}"; open(p,"wb").write(data)
    r=subprocess.run([LNK,"-hex","-o",f"{D}/{name}.hex",p],capture_output=True)
    crash = r.returncode<0 or r.returncode>=128
    good_ = (not crash) and ((r.returncode!=0)==expect_reject)
    print(("  \033[32mPASS\033[0m " if good_ else "  \033[31mFAIL\033[0m ")+f"reader: {name}")
    n_pass+=good_; n_fail+=(not good_)
b=bytearray(good); struct.pack_into("<H",b,4,999);       chk("bad_version",bytes(b))
b=bytearray(good); struct.pack_into("<H",b,6,0);         chk("bad_endian",bytes(b))
b=bytearray(good); struct.pack_into("<I",b,36,0xFFFFFFF0); struct.pack_into("<I",b,40,0x20); chk("dataoff_ovf",bytes(b))
b=bytearray(good); struct.pack_into("<I",b,20,0xFFFFFFF0); struct.pack_into("<I",b,24,0x20); chk("strtab_ovf",bytes(b))
b=bytearray(good); b[soff+ssz-1]=0x41;                    chk("strtab_no_nul",bytes(b))
b=bytearray(good); struct.pack_into("<I",b,8,0x20000000); chk("huge_seccount",bytes(b))
b=bytearray(good); struct.pack_into("<I",b,12,0x20000000);chk("huge_symcount",bytes(b))
b=bytearray(good); struct.pack_into("<I",b,16,0x20000000);chk("huge_relcount",bytes(b))
chk("truncated",good[:20])
chk("valid",good,expect_reject=False)
open(f"{D}/reader.res","w").write(f"{n_pass} {n_fail}")
PY
read rp rf < "$D/reader.res"; pass=$((pass+rp)); fail=$((fail+rf))

echo "== 6) kraj 32-bitnog adresnog prostora =="
exp_ok  "sekcija @0xFFFFFFFC (validno)"  $LNK -hex -place=s@0xFFFFFFFC -o "$D/x.hex" "$D/tiny.o"
exp_err "sekcija @0xFFFFFFFE (prelazi)"  "32-bitni"  $LNK -hex -place=s@0xFFFFFFFE -o "$D/x.hex" "$D/tiny.o"

echo "== 7) direct -hex == (-relocatable -> -hex) =="
$LNK -hex -place=my_code@0x40000000 -o "$D/d.hex" "$D/main.o" "$D/isr_terminal.o" "$D/isr_timer.o" "$D/handler.o" >/dev/null 2>&1
$LNK -relocatable -o "$D/m.o" "$D/main.o" "$D/isr_terminal.o" "$D/isr_timer.o" "$D/handler.o" >/dev/null 2>&1
$LNK -hex -place=my_code@0x40000000 -o "$D/r.hex" "$D/m.o" >/dev/null 2>&1
if diff -q "$D/d.hex" "$D/r.hex" >/dev/null; then ok "ekvivalencija direct/relocatable"; else bad "ekvivalencija razlika"; fi

echo
echo "REZULTAT: PASS=$pass FAIL=$fail"
rm -rf "$D"
[ "$fail" -eq 0 ]
