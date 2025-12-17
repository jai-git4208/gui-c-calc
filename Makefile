TARGET = calc
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
	CC = gcc
	CFLAGS = -Wall -g -I./lib -D__AMALG_M__ -DNVG_NO_STB -Wno-deprecated-declarations
	LDFLAGS = -lSDL2 -lSDL2_ttf -lGL -lm
	SRCS = main.c lib/nanovg.c
else
	CC = clang
	CFLAGS = -Wall -g -I./lib -D__AMALG_M__ -DNVG_NO_STB -Wno-deprecated-declarations
	LDFLAGS = -lSDL2 -lSDL2_ttf -framework OpenGL -lm -framework Cocoa
	SRCS = main.c lib/nanovg.c platform_mac.m
endif

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
