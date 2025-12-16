CC = clang
CFLAGS = -Wall -g -I./lib -D__AMALG_M__ -DNVG_NO_STB -Wno-deprecated-declarations -I./lib -D__AMALG_M__
LDFLAGS = -lSDL2 -lSDL2_ttf -framework OpenGL -lm -framework Cocoa

TARGET = calc
SRCS = main.c lib/nanovg.c platform_mac.m

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
