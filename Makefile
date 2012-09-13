# -----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# Lukas Niederbremer <webmaster@flippeh.de> and Clark Gaebel <cg.wowus.cg@gmail.com>
# wrote this file. As long as you retain this notice you can do whatever you
# want with this stuff. If we meet some day, and you think this stuff is worth
# it, you can buy us a beer in return.
# -----------------------------------------------------------------------------

CFLAGS=-g -Wall -Wextra -std=c99 -pedantic -fPIC
OBJS=buffer.o nbt_loading.o nbt_parsing.o nbt_treeops.o nbt_util.o mcr.o

all: nbtreader check regioninfo copychunk

nbtreader: main.o libnbt.a
	$(CC) $(CFLAGS) main.o -L. -lnbt -lz -o nbtreader

check: check.c libnbt.a
	$(CC) $(CFLAGS) check.c -L. -lnbt -lz -o check

regioninfo: regioninfo.c libnbt.a
	$(CC) $(CFLAGS) regioninfo.c -L. -lnbt -lz -o regioninfo

copychunk: copychunk.c libnbt.a
	$(CC) $(CFLAGS) copychunk.c -L. -lnbt -lz -o copychunk

test: check
	cd testdata && ls -1 *.nbt | xargs -n1 ../check && cd ..

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libnbt.a: $(OBJS)
	$(AR) -rcs libnbt.a $(OBJS)

clean:
	rm -rf $(OBJS) *.dSYM libnbt.a nbtreader check regioninfo
