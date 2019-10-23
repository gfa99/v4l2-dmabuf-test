#https://blog.csdn.net/yychuyu/article/details/79950414
#https://github.com/yychuyu/makefile-demo/tree/master/single

SOURCE = $(wildcard *.c)
TARGETS = $(patsubst %.c, %, $(SOURCE))
 
CC = gcc
CFLAGS = -Wall -g
 
all:$(TARGETS)
 
$(TARGETS):%:%.c
	$(CC) $< $(CFLAGS) -o $@
 
.PHONY:clean all
clean:
	-rm -rf $(TARGETS)

