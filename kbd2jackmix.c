#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <math.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include <fcntl.h>
#include <errno.h>

#define NEVENTS 1

#define MIDI_CONTROLLER 0xB0
#define KEY_SHFT_CODE 42
#define KEY_CTRL_CODE 29
#define KEY_UP_ARROW_CODE 103
#define KEY_DOWN_ARROW_CODE 108

struct MidiMessage {
	jack_nframes_t time;
	int	len;/*Length of MIDI message in bytes.*/
	unsigned char data[3];
};

#define RINGBUFFER_SIZE	1024*sizeof(struct MidiMessage)

jack_port_t *midi_port;
jack_client_t *client;
jack_ringbuffer_t *midiringbuf;
const char *err_buf;
int intval;

void process_midi_output(jack_nframes_t nframes) {
	int	read, t;
	unsigned char *buffer;
	void *port_buffer;
	jack_nframes_t last_frame_time;
	struct MidiMessage ev;
	last_frame_time = jack_last_frame_time(client);
	port_buffer = jack_port_get_buffer(midi_port, nframes);
	if (port_buffer == NULL) {
		return;
	}
	jack_midi_clear_buffer(port_buffer);
	while (jack_ringbuffer_read_space(midiringbuf)) {
		read = jack_ringbuffer_peek(midiringbuf, (char *)&ev, sizeof(ev));
		if (read != sizeof(ev)) {
			jack_ringbuffer_read_advance(midiringbuf, read);
			continue;
		}
		t = ev.time + nframes - last_frame_time;
		/* If computed time is too much into the future, we'll need
		   to send it later. */
		if (t >= (int)nframes)
			break;
		/* If computed time is < 0, we missed a cycle because of xrun. */
		if (t < 0)
			t = 0;
		jack_ringbuffer_read_advance(midiringbuf, sizeof(ev));
		buffer = jack_midi_event_reserve(port_buffer, t, ev.len);
		memcpy(buffer, ev.data, ev.len);
	}
}

void queue_message(struct MidiMessage *ev) {
	int written;
	if (jack_ringbuffer_write_space(midiringbuf) < sizeof(*ev)) {
		fprintf(stderr, "Not enough space in the ringbuffer, NOTE LOST.");
		return;
	}
	written = jack_ringbuffer_write(midiringbuf, (char *)ev, sizeof(*ev));
	if (written != sizeof(*ev))
		fprintf(stderr, "jack_ringbuffer_write failed, NOTE LOST.");
}

void queue_new_message(int b0, int b1, int b2) {
	struct MidiMessage ev;
	if (b1 == -1) {
		ev.len = 1;
		ev.data[0] = b0;
	} else if (b2 == -1) {
		ev.len = 2;
		ev.data[0] = b0;
		ev.data[1] = b1;
	} else {
		ev.len = 3;
		ev.data[0] = b0;
		ev.data[1] = b1;
		ev.data[2] = b2;
	}
	ev.time = jack_frame_time(client);
	queue_message(&ev);
}

static void signal_handler(int sig) {
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

int process (jack_nframes_t nframes, void *arg) {
  process_midi_output(nframes);
	return 0;
}

void setup_jack() {
  char *client_name;
  jack_options_t jack_options = JackNullOption;
  jack_status_t status;
  midiringbuf = jack_ringbuffer_create(RINGBUFFER_SIZE);
  client_name = (char *) malloc(80 * sizeof(char));
  strcpy(client_name, "kbd2jackmix");
  client = jack_client_open (client_name, jack_options, &status);
  if (client == NULL) {
    fprintf (stderr, "jack_client_open() failed, "
     "status = 0x%2.0x\n", status);
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
  jack_set_process_callback (client, process, NULL);
  midi_port = jack_port_register(client, "midi_out",
   JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  if (jack_activate (client)) {
    fprintf (stderr, "cannot activate client");
    exit (1);
  }
}

void setup_signal_handler() {
  signal(SIGQUIT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);
}

int main (int argc, char *argv[]) {
  int pd;
  int kd;
  int ret;
  int err;
  const char *keyboard;
  struct input_event inev;
  struct epoll_event ev;
  struct epoll_event events[NEVENTS];
  memset(&ev, 0, sizeof(ev));
  keyboard = "/dev/input/by-id/usb-CM_Storm_QuickFire_Rapid_keyboard-event-kbd";
  pd = epoll_create1(EPOLL_CLOEXEC);
  kd = open(keyboard, O_RDONLY);
  ev.events = EPOLLIN;
  ev.data.fd = kd;
  ret = epoll_ctl(pd, EPOLL_CTL_ADD, kd, &ev);
  if (ret == -1) {
    err = errno;
    fprintf(stderr, "Error: %s\n", strerror(err));
    return 1;
  }
  setup_jack();
  setup_signal_handler();
  int ctrl_pressed = 0;
  int shft_pressed = 0;
  int up_arrow_pressed = 0;
  int down_arrow_pressed = 0;
  int saw_key_up_down = 0;
  double volume = 0;
  for(;;) {
    ret = epoll_wait(pd, events, NEVENTS, -1);
    if (ret == -1) return 1;
    read(kd, &inev, sizeof(inev));
    if (inev.type == 4) {
      saw_key_up_down = 1;
      continue;
    }

    if (saw_key_up_down) {
      if (inev.type == 1)
        switch (inev.code) {
        case KEY_CTRL_CODE:
          ctrl_pressed = !ctrl_pressed;
          break;
        case KEY_SHFT_CODE:
          shft_pressed = !shft_pressed;
          break;
        case KEY_UP_ARROW_CODE:
          up_arrow_pressed = !up_arrow_pressed;
          break;
        case KEY_DOWN_ARROW_CODE:
          down_arrow_pressed = !down_arrow_pressed;
          break;
      }
      saw_key_up_down = 0;
    }
    switch (inev.code) {
      case KEY_UP_ARROW_CODE:
        if (shft_pressed && ctrl_pressed && up_arrow_pressed) {
          volume += 6;
          if (volume > 127) {
            volume = 127;
          }
          queue_new_message(MIDI_CONTROLLER, 11, volume);
        }
        break;
      case KEY_DOWN_ARROW_CODE:
        if (shft_pressed && ctrl_pressed && down_arrow_pressed) {
          volume -= 6;
          if (volume < 0) {
            volume = 0;
          }
          queue_new_message(MIDI_CONTROLLER, 11, volume);
        }
        break;
    }
  }
  jack_client_close (client);
  close(kd);
  close(pd);
  return 0;
}

