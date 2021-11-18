.PHONY: clean all

all: beep beep_melody

beep: beep.c
	$(CC) $(LDFLAGS) -o $@ $^

beep_melody: beep_melody.c
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -f beep
	rm -f beep_melody
