CFLAGS=-march=armv7-a -mfpu=neon -O3 -pedantic -std=c99 -Wall -ffast-math
LDFLAGS=-lm -lsndfile
TARGET=dither

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET) *.o sine.wav spectrogram.png

check: sweep.png
	xloadimage $^

%.png: %.wav
	sox $^ -n spectrogram -z100 -Z-40 -o $@ -c "$(shell echo $@ | sed -e 's/b\([0-9]\)-\([^-]*\)/, \2 dither\/2^\1/' -e 's/nodither dither/no dither/' -e 's/.png//' -e 's/-s0/, no noise shaping/' -e 's/-s255/, custom noise shaping/' -e 's/-s\([123]\)/, \1-tap noise shaping/' -e 's/-s4/, Lipshitz noise shaping/')"

sweep.wav: $(TARGET)
	./$(TARGET) -o $@

tone.wav: $(TARGET)
	./$(TARGET) -m0 -t1 -f1000 -o $@

tone-d%.wav: $(TARGET)
	./$(TARGET) -m0 -t1 -f1000 -d$* -o $@

tone-s%.wav: $(TARGET)
	./$(TARGET) -m0 -t1 -f1000 -d257 -s$* -o $@

sweep-nodither-s%.wav: $(TARGET)
	./$(TARGET) -b0 -d0 -s$* -o $@

sweep-white-s%.wav: $(TARGET)
	./$(TARGET) -b0 -d1 -s$* -o $@

sweep-violet-s%.wav: $(TARGET)
	./$(TARGET) -b0 -d2 -s$* -o $@

sweep-3tap-s%.wav: $(TARGET)
	./$(TARGET) -b0 -d3 -s$* -o $@

sweep-tpdf-s%.wav: $(TARGET)
	./$(TARGET) -b0 -d257 -s$* -o $@
