CFLAGS=-march=armv7-a -mfpu=neon -O3 -pedantic -std=c99 -Wall -ffast-math
LDFLAGS=-lm -lsndfile
TARGET=dither

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET) *.o sine.wav spectrogram.png

check: spectrogram.png
	xloadimage $^

spectrogram.png: sine.wav
	sox $^ -n spectrogram

sine.wav: $(TARGET)
	./$^

