#!/usr/bin/env bash
# Testovi linkera (nivo A i B): uspešni i neuspešni slučajevi za
# -hex, -relocatable, -place, višestruke definicije, nerazrešene simbole,
# preklapanje sekcija i pravilo "tačno jedna od -hex/-relocatable".
#
# Pokretanje iz korena projekta:  bash tests/linker/run_linker_tests.sh
set -u
cd "$(dirname "$0")/../.."          # koren projekta

ASM=./assembler
LNK=./linker
DIR=$(mktemp -d)
pass=0; fail=0

ok()   { printf "  \033[32mPASS\033[0m %s\n" "$1"; pass=$((pass+1)); }
bad()  { printf "  \033[31mFAIL\033[0m %s\n" "$1"; fail=$((fail+1)); }

# expect_ok  "opis"  <komanda...>
expect_ok()  { local d="$1"; shift; if "$@" >"$DIR/out" 2>"$DIR/err"; then ok "$d"; else bad "$d (očekivan uspeh, exit=$?)"; sed 's/^/      /' "$DIR/err"; fi; }
# expect_err "opis" "podniz-u-poruci"  <komanda...>
expect_err() { local d="$1" needle="$2"; shift 2;
  if "$@" >"$DIR/out" 2>"$DIR/err"; then bad "$d (očekivana greška, ali exit=0)";
  elif grep -q -- "$needle" "$DIR/err"; then ok "$d";
  else bad "$d (greška da, ali poruka ne sadrži '$needle')"; sed 's/^/      /' "$DIR/err"; fi; }

echo "== Asembliranje ulaza za linker =="
for s in dup1 dup2 provider consumer unres ov_a ov_b; do
  $ASM -o "$DIR/$s.o" "tests/linker/$s.s" >/dev/null 2>&1 || { echo "  ASM FAIL $s"; exit 1; }
done
# nivo-b program (pravi, međusobno povezani fajlovi)
for s in main handler isr_terminal isr_timer; do
  $ASM -o "$DIR/$s.o" "tests/nivo-b/$s.s" >/dev/null 2>&1 || { echo "  ASM FAIL $s"; exit 1; }
done

echo "== Opcije komandne linije =="
expect_err "nijedna od -hex/-relocatable"       "tačno jednu"      $LNK -o "$DIR/x.hex" "$DIR/provider.o"
expect_err "obe -hex i -relocatable"            "tačno jednu"      $LNK -hex -relocatable -o "$DIR/x" "$DIR/provider.o"
expect_err "nema ulaznih datoteka"              "nema ulaznih"     $LNK -hex -o "$DIR/x.hex"
expect_err "nepostojeći ulazni fajl"            "ne mogu da otvorim" $LNK -hex -o "$DIR/x.hex" "$DIR/nema.o"

echo "== -hex uspešni slučajevi =="
expect_ok  "hex: jedan fajl"                     $LNK -hex -o "$DIR/one.hex" "$DIR/provider.o"
expect_ok  "hex: više fajlova (provider+consumer)" $LNK -hex -o "$DIR/pc.hex" "$DIR/provider.o" "$DIR/consumer.o"
expect_ok  "hex: nivo-b ceo program"             $LNK -hex -place=my_code@0x40000000 -o "$DIR/prog.hex" \
              "$DIR/main.o" "$DIR/isr_terminal.o" "$DIR/isr_timer.o" "$DIR/handler.o"
expect_ok  "hex: -place više sekcija"            $LNK -hex -place=sa@0x1000 -place=sb@0x2000 -o "$DIR/pl.hex" \
              "$DIR/ov_a.o" "$DIR/ov_b.o"

echo "== -hex greške =="
expect_err "hex: nerazrešen simbol"              "missing_sym"      $LNK -hex -o "$DIR/u.hex" "$DIR/unres.o"
expect_err "hex: višestruka definicija"          "foo"              $LNK -hex -o "$DIR/d.hex" "$DIR/dup1.o" "$DIR/dup2.o"
expect_err "hex: preklapanje sekcija"            "preklapaju"       $LNK -hex -place=sa@0x1000 -place=sb@0x1008 -o "$DIR/o.hex" \
              "$DIR/ov_a.o" "$DIR/ov_b.o"
expect_err "hex: -place nepostojeća sekcija"     "nepostoje"        $LNK -hex -place=nema@0x1000 -o "$DIR/np.hex" "$DIR/provider.o"

echo "== -relocatable =="
expect_ok  "reloc: spajanje (nerazrešen dozvoljen)" $LNK -relocatable -o "$DIR/m.o" "$DIR/consumer.o"
expect_ok  "reloc: nivo-b spajanje"              $LNK -relocatable -o "$DIR/mb.o" \
              "$DIR/main.o" "$DIR/isr_terminal.o" "$DIR/isr_timer.o" "$DIR/handler.o"
expect_err "reloc: višestruka definicija"        "foo"              $LNK -relocatable -o "$DIR/md.o" "$DIR/dup1.o" "$DIR/dup2.o"

echo "== Round-trip: (reloc -> hex) == (direct hex) =="
$LNK -relocatable -o "$DIR/rt.o" "$DIR/main.o" "$DIR/isr_terminal.o" "$DIR/isr_timer.o" "$DIR/handler.o" >/dev/null 2>&1
$LNK -hex -place=my_code@0x40000000 -o "$DIR/rt.hex" "$DIR/rt.o" >/dev/null 2>&1
if diff -q "$DIR/prog.hex" "$DIR/rt.hex" >/dev/null; then ok "round-trip identičan direktnom hex-u"; else bad "round-trip razlika"; fi

echo
echo "REZULTAT: PASS=$pass FAIL=$fail"
rm -rf "$DIR"
[ "$fail" -eq 0 ]
