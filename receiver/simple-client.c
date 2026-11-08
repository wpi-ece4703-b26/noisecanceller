// lmscancel.c - Pi 2: LMS noise canceller
//
// input_left   : x[n] + n0[n]   primary   (signal + noise)
// input_right  : n1[n]          reference (noise only)
// output_left  : x[n] + n0[n]   received dirty signal (copy of input_left)
// output_right : e[n]           cleaned signal
//
// LMS: input n1[n], desired x[n] + n0[n], error e[n] = d[n] - y[n]
// The LMS coefficients converge to H(z), the filter on Pi 1.
//
// keyboard commands (type + Enter):
//   d   dump LMS coefficients and error
//   g   text graph of LMS coefficients
//   r   reset LMS coefficients
//   q   quit

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include <jack/jack.h>

#define NTAPS 16
#define MU    0.001f

jack_port_t *input_port_left;
jack_port_t *input_port_right;
jack_port_t *output_port_left;
jack_port_t *output_port_right;
jack_client_t *client;

volatile int reset = 0;   // set by keyboard, cleared in process()
float errpower = 0.0f;    // smoothed e^2

// LMS filter

float lmsx[NTAPS];   // input history, lmsx[0] is newest
float lmsw[NTAPS];   // coefficients

float lms(float x, float d) {
    float y = 0.0f;

    for (int k = NTAPS - 1; k > 0; k--)
        lmsx[k] = lmsx[k-1];
    lmsx[0] = x;

    for (int k = 0; k < NTAPS; k++)
        y += lmsw[k] * lmsx[k];

    float e = d - y;

    for (int k = 0; k < NTAPS; k++)
        lmsw[k] += MU * e * lmsx[k];

    return e;
}

// text graph of the coefficients, 16 lines, scaled to the largest |w|

void graph() {
    float m = 0.0f;
    for (int k = 0; k < NTAPS; k++)
        if (fabsf(lmsw[k]) > m)
            m = fabsf(lmsw[k]);
    if (m == 0.0f)
        m = 1.0f;

    for (int r = 0; r < 16; r++) {
        int level = (r < 8) ? 8 - r : 7 - r;   // +8 .. +1, -1 .. -8
        printf("%9.5f |", m * level / 8.0f);
        for (int k = 0; k < NTAPS; k++) {
            int h = (int) roundf(8.0f * lmsw[k] / m);
            if ((level > 0 && h >= level) || (level < 0 && h <= level))
                printf(" #");
            else
                printf("  ");
        }
        printf("\n");
    }
}

int process (jack_nframes_t nframes, void *arg) {
  jack_default_audio_sample_t *inl, *inr, *outl, *outr;

  inl  = jack_port_get_buffer (input_port_left, nframes);
  inr  = jack_port_get_buffer (input_port_right, nframes);
  outl = jack_port_get_buffer (output_port_left, nframes);
  outr = jack_port_get_buffer (output_port_right, nframes);

  if (reset) {
    memset(lmsw, 0, sizeof(lmsw));
    reset = 0;
  }

  for (jack_nframes_t i = 0; i < nframes; i++) {
    float d = inl[i];          // read inputs before writing outputs
    float e = lms(inr[i], d);
    errpower += 0.001f * (e * e - errpower);
    outl[i] = d;
    outr[i] = e;
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

  printf ("engine sample rate: %" PRIu32 "\n", jack_get_sample_rate (client));

  input_port_left = jack_port_register (client, "input_left",
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsInput, 0);
  input_port_right = jack_port_register (client, "input_right",
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsInput, 0);
  output_port_left = jack_port_register (client, "output_left",
                                    JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);
  output_port_right = jack_port_register (client, "output_right",
                                    JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);

  if ((input_port_left == NULL) || (input_port_right == NULL) ||
      (output_port_left == NULL) || (output_port_right == NULL)) {
    fprintf(stderr, "no more JACK ports available\n");
    exit (1);
  }

  if (jack_activate (client)) {
    fprintf (stderr, "cannot activate client");
    exit (1);
  }

  printf("commands: d=dump g=graph r=reset q=quit\n");

  char line[80];
  while (fgets(line, sizeof(line), stdin)) {
    if (line[0] == 'r') {
      reset = 1;
      printf("reset\n");
    } else if (line[0] == 'd') {
      for (int k = 0; k < NTAPS; k += 4)
        printf("w[%2d..%2d] = %9.5f %9.5f %9.5f %9.5f\n", k, k + 3,
               lmsw[k], lmsw[k+1], lmsw[k+2], lmsw[k+3]);
      printf("error rms = %9.5f\n", sqrtf(errpower));
    } else if (line[0] == 'g') {
      graph();
    } else if (line[0] == 'q') {
      break;
    }
  }

  jack_client_close (client);

  exit (0);
}
