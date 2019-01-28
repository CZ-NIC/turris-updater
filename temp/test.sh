#! /bin/sh
# gcc -g -O0 -o rls ls.c
gcc -g -O0 -o rls ../src/lib/file-funcs.c
rm -rf dir
./makefiles.sh
# ./rls dir
# valgrind --log-file=valgrind.log --leak-check=full --track-origins=yes --show-reachable=yes ./rls dir
valgrind -v --leak-check=full --track-origins=yes --show-reachable=yes ./rls dir
