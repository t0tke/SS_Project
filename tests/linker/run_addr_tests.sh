#!/usr/bin/env bash
# Testovi ponašanja linkera nad VALIDNIM SSOB fajlovima (asembler / linker -relocatable):
#  - kraj 32-bitnog adresnog prostora (sekcija sme do 0x100000000, ali simbol/naredna
#    sekcija ne smeju da se obmotaju na 0);
#  - lokalan nedefinisan simbol se ne razrešava globalom niti postaje global u -relocatable;
#  - ekvivalencija direct -hex  ==  (-relocatable -> -hex);
#  - regresija: agregiranje, globali, relokacije, -place, nerazrešeni, preklapanja, exit.
#
# Pokretanje iz korena projekta:  bash tests/linker/run_addr_tests.sh
set -u
cd "$(dirname "$0")/../.."
ASM=./assembler; LNK=./linker
D=$(mktemp -d); pass=0; fail=0
ok()  { printf "  \033[32mPASS\033[0m %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  \033[31mFAIL\033[0m %s\n" "$1"; fail=$((fail+1)); }
exp_ok()  { local d="$1"; shift; if "$@" >/dev/null 2>"$D/e"; then ok "$d"; else bad "$d (exit=$?)"; sed 's/^/      /' "$D/e"; fi; }
exp_err() { local d="$1" n="$2"; shift 2; local o rc; o=$("$@" 2>&1 >/dev/null); rc=$?
  if [ $rc -eq 0 ]; then bad "$d (exit=0)"; elif echo "$o" | grep -q -- "$n"; then ok "$d"; else bad "$d (poruka: $o)"; fi; }

for s in topsym twosec; do $ASM -o "$D/$s.o" "tests/linker/$s.s" >/dev/null 2>&1 || { echo "ASM FAIL $s"; exit 1; }; done
for s in gdef localref; do $ASM -o "$D/$s.o" "tests/linker/$s.s" >/dev/null 2>&1 || { echo "ASM FAIL $s"; exit 1; }; done
for s in main handler isr_terminal isr_timer; do $ASM -o "$D/$s.o" "tests/nivo-b/$s.s" >/dev/null 2>&1 || exit 1; done

echo "== kraj 32-bitnog adresnog prostora =="
exp_ok  "sekcija se završava tačno na 0x100000000 (simbol nije na kraju)" \
        $LNK -hex -place=s@0xFFFFFFF8 -o "$D/a.hex" "$D/topsym.o"
exp_err "simbol na 0x100000000 se obmotava"        "van 32-bitnog"  $LNK -hex -place=s@0xFFFFFFFC -o "$D/b.hex" "$D/topsym.o"
exp_err "naredna sekcija bi se obmotala na 0"       "van 32-bitnog"  $LNK -hex -place=a@0xFFFFFFFC -o "$D/c.hex" "$D/twosec.o"

echo "== lokalan nedefinisan simbol (oba režima) =="
exp_err "-hex: local-undef ne hvata global"         "nedefinisan lokalan" $LNK -hex -o "$D/x.hex" "$D/localref.o" "$D/gdef.o"
exp_err "-relocatable: local-undef ne postaje global" "nedefinisan lokalan" $LNK -relocatable -o "$D/x.o" "$D/localref.o"
exp_err "-relocatable: local-undef + gdef"          "nedefinisan lokalan" $LNK -relocatable -o "$D/x.o" "$D/localref.o" "$D/gdef.o"

echo "== ekvivalencija direct -hex == (-relocatable -> -hex) =="
# a) ceo nivo-b program (agregacija sekcije isr iz dva fajla + unakrsni globali)
$LNK -hex -place=my_code@0x40000000 -o "$D/d1.hex" "$D/main.o" "$D/isr_terminal.o" "$D/isr_timer.o" "$D/handler.o" >/dev/null 2>&1
$LNK -relocatable -o "$D/m1.o" "$D/main.o" "$D/isr_terminal.o" "$D/isr_timer.o" "$D/handler.o" >/dev/null 2>&1
$LNK -hex -place=my_code@0x40000000 -o "$D/r1.hex" "$D/m1.o" >/dev/null 2>&1
diff -q "$D/d1.hex" "$D/r1.hex" >/dev/null && ok "nivo-b ekvivalencija" || bad "nivo-b ekvivalencija"
# b) blizu vrha adresnog prostora (validno)
$LNK -hex -place=s@0xFFFFFFF8 -o "$D/d2.hex" "$D/topsym.o" >/dev/null 2>&1
$LNK -relocatable -o "$D/m2.o" "$D/topsym.o" >/dev/null 2>&1
$LNK -hex -place=s@0xFFFFFFF8 -o "$D/r2.hex" "$D/m2.o" >/dev/null 2>&1
diff -q "$D/d2.hex" "$D/r2.hex" >/dev/null && ok "ekvivalencija blizu vrha" || bad "ekvivalencija blizu vrha"
# c) wrap slučaj: oba moraju greška
$LNK -hex -place=s@0xFFFFFFFC -o "$D/d3.hex" "$D/topsym.o" >"$D/o1" 2>&1; e1=$?
$LNK -hex -place=s@0xFFFFFFFC -o "$D/r3.hex" "$D/m2.o"     >"$D/o2" 2>&1; e2=$?
[ $e1 -ne 0 ] && [ $e2 -ne 0 ] && ok "wrap: oba režima greška" || bad "wrap: nekonzistentno (d=$e1 r=$e2)"

echo
echo "REZULTAT: PASS=$pass FAIL=$fail"
rm -rf "$D"
[ "$fail" -eq 0 ]
