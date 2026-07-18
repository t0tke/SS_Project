#!/usr/bin/env bash
# Prazna sekcija / prazno parče koje bi počelo tačno na 0x100000000.
# Baza takve (i prazne) sekcije/parčeta ne sme da se obmota na 0 pri kastovanju
# u uint32_t; linker mora prijaviti grešku PRE kastovanja. Validne varijante
# (nisu na samom kraju) rade i daju isti rezultat u direct i relocatable->hex.
#
# Pokretanje iz korena projekta:  bash tests/linker/run_empty_tests.sh
set -u
cd "$(dirname "$0")/../.."
ASM=./assembler; LNK=./linker
D=$(mktemp -d); pass=0; fail=0
ok()  { printf "  \033[32mPASS\033[0m %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  \033[31mFAIL\033[0m %s\n" "$1"; fail=$((fail+1)); }
exp_ok()  { local d="$1"; shift; if "$@" >/dev/null 2>"$D/e"; then ok "$d"; else bad "$d (exit=$?)"; sed 's/^/      /' "$D/e"; fi; }
exp_err() { local d="$1" n="$2"; shift 2; local o rc; o=$("$@" 2>&1 >/dev/null); rc=$?
  if [ $rc -eq 0 ]; then bad "$d (exit=0)"; elif echo "$o" | grep -q -- "$n"; then ok "$d"; else bad "$d ($o)"; fi; }

for s in emptysec pieceA pieceB; do $ASM -o "$D/$s.o" "tests/linker/$s.s" >/dev/null 2>&1 || { echo "ASM FAIL $s"; exit 1; }; done

echo "== prazna SEKCIJA koja bi počela na 0x100000000 =="
exp_err "prazna sekcija na 0x100000000 -> greška" "van 32-bitnog" \
        $LNK -hex -place=a@0xFFFFFFFC -o "$D/x.hex" "$D/emptysec.o"
exp_ok  "prazna sekcija @0xFFFFFFF0 (validno)" \
        $LNK -hex -place=a@0xFFFFFFF0 -o "$D/e.hex" "$D/emptysec.o"

echo "== prazno PARČE koje bi počelo na 0x100000000 =="
exp_err "prazno parče na 0x100000000 -> greška" "van 32-bitnog" \
        $LNK -hex -place=s@0xFFFFFFFC -o "$D/y.hex" "$D/pieceA.o" "$D/pieceB.o"
exp_ok  "prazno parče @0xFFFFFFF0 (validno)" \
        $LNK -hex -place=s@0xFFFFFFF0 -o "$D/p.hex" "$D/pieceA.o" "$D/pieceB.o"

echo "== ekvivalencija direct -hex == (-relocatable -> -hex) =="
# emptysec (validna varijanta)
$LNK -relocatable -o "$D/em.o" "$D/emptysec.o" >/dev/null 2>&1
$LNK -hex -place=a@0xFFFFFFF0 -o "$D/er.hex" "$D/em.o" >/dev/null 2>&1
diff -q "$D/e.hex" "$D/er.hex" >/dev/null && ok "emptysec ekvivalencija" || bad "emptysec ekvivalencija"
# piece (validna varijanta — pokriva i nr.addend pomeranje za parče)
$LNK -relocatable -o "$D/pm.o" "$D/pieceA.o" "$D/pieceB.o" >/dev/null 2>&1
$LNK -hex -place=s@0xFFFFFFF0 -o "$D/pr.hex" "$D/pm.o" >/dev/null 2>&1
diff -q "$D/p.hex" "$D/pr.hex" >/dev/null && ok "piece ekvivalencija" || bad "piece ekvivalencija"
# wrap varijante: oba režima greška
$LNK -hex -place=a@0xFFFFFFFC -o "$D/x1" "$D/em.o" >/dev/null 2>&1; e1=$?
$LNK -hex -place=s@0xFFFFFFFC -o "$D/x2" "$D/pm.o" >/dev/null 2>&1; e2=$?
[ $e1 -ne 0 ] && [ $e2 -ne 0 ] && ok "wrap: oba režima greška" || bad "wrap: nekonzistentno (e=$e1 p=$e2)"

echo
echo "REZULTAT: PASS=$pass FAIL=$fail"
rm -rf "$D"
[ "$fail" -eq 0 ]
