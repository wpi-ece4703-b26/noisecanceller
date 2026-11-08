// noisegen.c - Pi 1: signal + filtered noise source for the LMS noise canceller
//
// output_left  : x[n] + H(n1[n])   primary   (1 kHz sine + interference through H)
// output_right : n1[n]             reference (the interference itself)
//
// H(z) is an 8 tap FIR. The LMS on Pi 2 should find these coefficients.
//
// keyboard commands (type + Enter):
//   n   interference = white noise
//   b   interference = 470 Hz block wave (shows up as spikes, easy to see on a scope)
//   q   quit

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include <jack/jack.h>

#define FIRTAPS 8

// amplitude of the sine and of the interference
#define AMPLITUDE 1.0f

jack_port_t *output_port_left;
jack_port_t *output_port_right;
jack_client_t *client;

// sine generator

float phase = 0.0f;
float phaseinc;

float sine() {
    float s = sinf(phase);
    phase += phaseinc;
    if (phase >= 2.0f * M_PI)
        phase -= 2.0f * M_PI;
    return AMPLITUDE * s;
}

volatile int blockwave = 0;   // 0 = noise, 1 = block wave

// block wave generator, 470 Hz (no harmonic at 1 kHz), between -AMPLITUDE and AMPLITUDE

float bphase = 0.0f;
float bphaseinc;

float block() {
    bphase += bphaseinc;
    if (bphase >= 2.0f * M_PI)
        bphase -= 2.0f * M_PI;
    return (bphase < M_PI) ? AMPLITUDE : -AMPLITUDE;
}

// noise generator, uniform between -AMPLITUDE and AMPLITUDE

uint32_t seed = 22222;

float noise() {
    seed ^= seed << 13;    // xorshift random generator
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return AMPLITUDE * (seed * (2.0f / 4294967296.0f) - 1.0f);
}

// FIR filter H(z)

typedef struct firstate {
    float s[FIRTAPS];  // filter state, s[0] is newest
    float c[FIRTAPS];  // filter coefficients
} firstate_t;

float fir(float x, firstate_t *p) {
    float r = 0.0f;
    for (int k = FIRTAPS - 1; k > 0; k--)
        p->s[k] = p->s[k-1];
    p->s[0] = x;
    for (int k = 0; k < FIRTAPS; k++)
        r += p->s[k] * p->c[k];
    return r;
}

void createfir(const float *c,
               firstate_t *p) {
    for (int k = 0; k < FIRTAPS; k++) {
        p->c[k] = c[k];
        p->s[k] = 0.0f;
    }
}

float coefs[FIRTAPS] = { 0.5f, 0.0f, 0.0f, -0.5f, 0.25f, 0.0f, 0.0f, -0.25f };

firstate_t h;

int process (jack_nframes_t nframes, void *arg) {
  jack_default_audio_sample_t *outl, *outr;

  outl = jack_port_get_buffer (output_port_left, nframes);
  outr = jack_port_get_buffer (output_port_right, nframes);

  for (jack_nframes_t i = 0; i < nframes; i++) {
    float n1 = blockwave ? block() : noise();
    outl[i] = sine() + fir(n1, &h);
    outr[i] = n1;
  }

  return 0;
}

void jack_shutdown (void *arg) {
        exit (1);
}

int main (int argc, char *argv[]) {
  const char *client_name = "simple";
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  client = jack_client_open (client_name, options, &status, server_name);
  if (client == NULL) {
    fprintf (stderr, "jack_client_open() failed, status = 0x%02x\n", status);
    if (status & JackServerFailed) {
      fprintf (stderr, "Unable to connect to JACK server\n");
    }
    exit (1);
  }
  if (status & JackServerStarted) {
    fprintf (stderr, "JACK server started\n");
  }
  if (status & JackNameNotUnique) {
    client_name = jack_get_client_name(client);
    fprintf (stderr, "unique name `%s' assigned\n", client_name);
  }

  jack_set_process_callback (client, process, 0);
  jack_on_shutdown (client, jack_shutdown, 0);

  phaseinc  = 2.0f * M_PI * 1000.0f / jack_get_sample_rate (client);
  bphaseinc = 2.0f * M_PI *  470.0f / jack_get_sample_rate (client);
  createfir(coefs, &h);

  printf ("engine sample rate: %" PRIu32 "\n", jack_get_sample_rate (client));
  printf ("H(z) coefficients:");
  for (int k = 0; k < FIRTAPS; k++)
    printf (" %g", h.c[k]);
  printf ("\n");

  output_port_left = jack_port_register (client, "output_left",
                                    JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);
  output_port_right = jack_port_register (client, "output_right",
                                    JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);

  if ((output_port_left == NULL) || (output_port_right == NULL)) {
    fprintf(stderr, "no more JACK ports available\n");
    exit (1);
  }

  if (jack_activate (client)) {
    fprintf (stderr, "cannot activate client");
    exit (1);
  }

  printf("commands: n=noise b=block q=quit\n");

  char line[80];
  while (fgets(line, sizeof(line), stdin)) {
    if (line[0] == 'n') {
      blockwave = 0;
      printf("noise\n");
    } else if (line[0] == 'b') {
      blockwave = 1;
      printf("block wave\n");
    } else if (line[0] == 'q') {
      break;
    }
  }

  jack_client_close (client);

  exit (0);
}
