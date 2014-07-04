#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include <glib-2.0/glib.h>
#include <gtkmm.h>
#include <notification.h>
#include <notify.h>
#include <gdk/gdk.h>
#include <gdkmm-2.4/gdkmm/pixbufloader.h>
#include <gdkmm-2.4/gdkmm/pixbuf.h>
#include <festival.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <linux/input.h>

#define NEVENTS 1

#define SAY_BUF_SIZE 8000
#define MIDI_CONTROLLER 0xB0
#define KEY_SHFT_CODE 42
#define KEY_CTRL_CODE 29
#define KEY_UP_ARROW_CODE 103
#define KEY_DOWN_ARROW_CODE 108

struct MidiMessage {
	jack_nframes_t	time;
	int		len;	/* Length of MIDI message, in bytes. */
	unsigned char	data[3];
};

#define RINGBUFFER_SIZE		1024*sizeof(struct MidiMessage)

typedef jack_default_audio_sample_t sample_t;

NotifyNotification *notification;

jack_port_t  *my_output_ports[2], *midi_port;
jack_client_t *client;
jack_ringbuffer_t *jringbuf, *midiringbuf;
jack_nframes_t jsample_rate = 48000;

const char *err_buf;
int intval;

void log (const char *pattern, ...)
{
	va_list arguments;
	FILE *logfile;
	logfile = fopen ("/tmp/DDJ.log", "a");
	fprintf (logfile, pattern, arguments);
	fclose(logfile);
}

void process_midi_output(jack_nframes_t nframes) {
	int		read, t;
	unsigned char  *buffer;
	void           *port_buffer;
	jack_nframes_t	last_frame_time;
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

Glib::RefPtr< Gdk::Pixbuf > scale_pixbuf( Glib::RefPtr< Gdk::Pixbuf > const& pixbuf ) {
  const int width = pixbuf->get_width();
  const int height = pixbuf->get_height();
  int dest_width = 128;
  int dest_height = 128;
  double ratio = width / static_cast< double >(height);
  if( width > height ) {
    dest_height = static_cast< int >(dest_height / ratio);
  }
  else if( height > width ) {
    dest_width = static_cast< int >(dest_width * ratio);
  }
  return pixbuf->scale_simple( dest_width, dest_height, Gdk::INTERP_BILINEAR );
}

static void signal_handler(int sig) {
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

void festival_Synth(const char *text_to_say) {
  /*Strip out annoying [Explicit] from title.*/
  std::string edited_text(text_to_say);
  std::size_t found = edited_text.find("[Explicit]");
  if (found != edited_text.npos) {
    edited_text.erase(found, 10);
  }

  /*Replace '&' with 'and'.*/
  found = edited_text.find(" & ");
  while (found != edited_text.npos) {
    std::string replacement(" and ");
    edited_text.replace(found, 3, replacement, 0, replacement.size());
    found = edited_text.find(" & ");
  }

  /*Replace 'feat.' with 'featuring'.*/
  std::string toReplace = "feat.";
  found = edited_text.find(toReplace);
  if (found != edited_text.npos) {
    std::string replacement("featuring");
    edited_text.replace(found, toReplace.size(), replacement, 0, replacement.size());
  }

  /*Replace '[' with ''.*/
  found = edited_text.find("[");
  while (found != edited_text.npos) {
    std::string replacement("");
    edited_text.replace(found, 1, replacement, 0, replacement.size());
    found = edited_text.find("[");
  }

  /*Replace ']' with ''.*/
  found = edited_text.find("]");
  while (found != edited_text.npos) {
    std::string replacement("");
    edited_text.replace(found, 1, replacement, 0, replacement.size());
    found = edited_text.find("]");
  }

  EST_Wave wave;
  festival_text_to_wave(edited_text.c_str(), wave);
  double scale = 1/32768.0;
  wave.resample(jsample_rate);

  int numsamples = wave.num_samples();
  sample_t jbuf[numsamples];

  for (int i = 0; i < numsamples; i++) {
    jbuf[i] =  wave(i) * scale;
  }

  size_t num_bytes_to_write;

  num_bytes_to_write = numsamples*sizeof(sample_t);

  do {
    int nwritten = jack_ringbuffer_write(jringbuf, (char*)jbuf, num_bytes_to_write);
    if (nwritten < num_bytes_to_write && num_bytes_to_write > 0) {
      usleep(100000);
    }
    num_bytes_to_write -= nwritten;

  } while (num_bytes_to_write > 0);
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */

int process (jack_nframes_t nframes, void *arg)
{
  sample_t *out[2];
	size_t num_bytes_to_write, num_bytes_written, num_samples_written;
	int i;
	num_bytes_to_write = sizeof(sample_t) * nframes;
	for (i = 0; i < 2; i++) {
		out[i] = (sample_t *) jack_port_get_buffer (my_output_ports[i], nframes);
	}
	num_bytes_written = jack_ringbuffer_read(jringbuf, (char*)out[0], num_bytes_to_write);
	num_samples_written = num_bytes_written / sizeof(sample_t);
	for (i = 0; i < num_samples_written; i++) {
		out[1][i] = out[0][i];
	}
	for (i = num_samples_written; i < nframes; i++) {
		out[0][i] = out[1][i] = 0.0;
	}
  process_midi_output(nframes);
	return 0;
}

void setup_jack() {
  char *client_name;
  jack_options_t jack_options = JackNullOption;
  jack_status_t status;

  jringbuf = jack_ringbuffer_create(3276800);
  midiringbuf = jack_ringbuffer_create(RINGBUFFER_SIZE);
  client_name = (char *) malloc(80 * sizeof(char));

  char *tmp = "xmms2DJ and keyboard midi";

  strcpy(client_name, tmp);
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

  jsample_rate = jack_get_sample_rate(client);
  jack_set_process_callback (client, process, NULL);
  //jack_set_sync_callback (client, sync_callback, NULL);
  /* create stereo out ports */

  midi_port = jack_port_register(client, "midi_out",
   JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

  my_output_ports[0] = jack_port_register (client, "output_l",
                                           JACK_DEFAULT_AUDIO_TYPE,
                                           JackPortIsOutput, 0);
  my_output_ports[1] = jack_port_register (client, "output_r",
                                           JACK_DEFAULT_AUDIO_TYPE,
                                           JackPortIsOutput, 0);

  if ((my_output_ports[0] == NULL) ||
      (my_output_ports[1] == NULL)) {
    fprintf(stderr, "no more JACK ports available\n");
    exit (1);
  }

  if (jack_activate (client)) {
    fprintf (stderr, "cannot activate client");
    exit (1);
  }
}

void setup_festival() {
  int heap_size = 320000;  // default scheme heap size
  int load_init_files = 1; // we want the festival init files loaded
  festival_initialize(load_init_files,heap_size);
  festival_eval_command("(voice_cmu_us_slt_arctic_clunits)");
  festival_Synth("Hi.  I am your synthetic xmms2, jack DJ.  Let's Rock!");
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
  char *keyboard;
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
  notify_init("xmms2-jack-dj");
  notification = notify_notification_new("", NULL, NULL);
  setup_jack();
  setup_festival();
  setup_signal_handler();
  int ctrl_pressed = 0;
  int shft_pressed = 0;
  int up_arrow_pressed = 0;
  int down_arrow_pressed = 0;
  int saw_key_up_down = 0;
  int volume = 0;
  for(;;) {
    ret = epoll_wait(pd, events, NEVENTS, -1);
    if (ret == -1) return 1;
    read(kd, &inev, sizeof(inev));
    /*printf("ev.type %d ev.code %d\n", inev.type, inev.code);*/
    if (inev.type == 4) {
      saw_key_up_down = 1;
      continue;
    }

    if (saw_key_up_down) {
      if (inev.type == 1)
        switch (inev.code) {
        case KEY_CTRL_CODE:
          ctrl_pressed = !ctrl_pressed;
          fprintf(stderr, "ctrl %d\n", ctrl_pressed);
          break;
        case KEY_SHFT_CODE:
          shft_pressed = !shft_pressed;
          fprintf(stderr, "shft %d\n", shft_pressed);
          break;
        case KEY_UP_ARROW_CODE:
          up_arrow_pressed = !up_arrow_pressed;
          fprintf(stderr, "up_arrow %d\n", up_arrow_pressed);
          break;
        case KEY_DOWN_ARROW_CODE:
          down_arrow_pressed = !down_arrow_pressed;
          fprintf(stderr, "down_arrow %d\n", down_arrow_pressed);
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

