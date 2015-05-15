CC=gcc
CFLAGS=-g
LIB=
SENDPKT_OBJ=sendpkt.o
EXE=sendpkt

all:	$(EXE)

sendpkt: $(SENDPKT_OBJ)
	$(CC) $(CFLAGS) $(SENDPKT_OBJ) $(LIB) -o $@

clean:
	rm -rf *.o $(EXE)
