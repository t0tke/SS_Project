as -o hello.o hello.s
# readelf -a hello.o

ld --entry=custom_entry -o hello hello.o
./hello
# rm hello