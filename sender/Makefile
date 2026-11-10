simple-client: simple-client.c
		gcc simple-client.c -o simple-client -ljack -lm

run:
		pw-jack ./simple-client

linkports:
		pw-link simple:output_left alsa_output.platform-soc_sound.stereo-fallback:playback_FL
		pw-link simple:output_right alsa_output.platform-soc_sound.stereo-fallback:playback_FR

clean:
		rm -f *~ simple-client
