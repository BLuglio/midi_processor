HDIR=./include
WORKDIR=./src
CC=gcc
ODIR=./dist/
SRC=./src/
LDIR =./lib
LIBPATH=-L$(LDIR)
FRAMEWORKS=-framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework CoreServices -framework Carbon

CFLAGS=-I$(HDIR) -I$(WORKDIR)

build: main

main: main.o
	$(CC) $(ODIR)main.o -o $(ODIR)main $(FRAMEWORKS) $(LIBPATH) -lportmidi -lportaudio
	rm $(ODIR)main.o

%.o: $(SRC)%.c
	$(CC) $(CFLAGS) -c $^ -o $(ODIR)$@

debug: debug.o
	$(CC) -g $(ODIR)debug.o -o $(ODIR)debug $(FRAMEWORKS) $(LIBPATH) -lportmidi -lportaudio

debug.o: $(SRC)main.c
	$(CC) -g $(CFLAGS) -c $^ -o $(ODIR)$@

.PHONY: clean

clean:
	rm -f $(ODIR)/*

run: build
	$(ODIR)main



