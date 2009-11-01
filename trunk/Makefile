CC = gcc
RM = rm

TARGET = RokuFinder
CFLAGS += -O2 -Wall
DEFINES = -DVERSION=\"0.1\"
INCLUDE_PATHS += -I.

C_SRC = RokuFinder.c

OBJECTS = $(C_SRC:%.c=%.o)

LIBS =

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDE_PATHS) -o $@ -c $<

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS) -o $(TARGET)

clean:
	$(RM) -f *.o
	$(RM) -f $(TARGET)
