gcc -o generator generator.c
./generator
rm generator
# readelf -a generated.o

ld --entry=custom_entry -o generated generated.o
./generated
# rm generated