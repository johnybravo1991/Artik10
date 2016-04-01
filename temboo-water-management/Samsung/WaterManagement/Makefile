TARGET := WaterManagement
CC := gcc


CFLAGS := -O3 -Wall -Wno-deprecated-declarations
LDFLAGS := -L/opt/iothub/artik/temboo/temboo_artik_library/lib
INCFLAGS := -I/opt/iothub/artik/temboo/temboo_artik_library/include
LDLIBS := -ltemboo
.PHONY: all clean cleanall

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCFLAGS) $(LDLIBS) $(TARGET).c -o $(TARGET)

clean:
	$(RM) -r $(TARGET)

cleanall: clean

re: cleanall all