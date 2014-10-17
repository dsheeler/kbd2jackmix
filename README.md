kbd2jackmix
===========

Listen on a keyboard event device and respond to key combinations with jack midi volume messages for use with a midi-aware jack mixer, like jackmix or jack_mixer.

Build the kbd2jackmix executable by typing 'make'.  'sudo make install' will put it by default the executable, 'kbd2jackmix', in /usr/local/bin.

Usage is

% kbd2jackmix /dev/input/eventfile

Basically, everything is hardcoded except the path to the keyboard event device (e.g., /dev/input/event17), which you must give as a command line argument.  Other stuff that is hardcoded: the key-bindings and the midi channels.  It needs a configuration interface for these things. In its initial, hardcoded configuration, pressing ctl-shift-up_arrow increases a stored volume value (initialized to zero) and sends the volume via channel 11 over the jack midi port, and ctl-shift-down_arrow decreases the stored volume and sends it.

One bit of setup is necessary, the user account used to run kbd2jackmix must have read access to the keyboard device which is by default owned by root with permissions 700.  I give the file permissions 770 and change its group to something my user account belongs to, namely the 'audio' group, for me, since I must be in that group for jack permissions.  Conveniently, I run these two commands once per re-boot:


 % sudo chmod 770 /dev/input/event17

 % sudo chgrp audio /dev/input/event17

To identify my keyboard device, I run this:

$ ls -l /dev/input/by-id/
total 0
lrwxrwxrwx 1 root root 10 Jul 30 18:10 usb-046d_081b_E264B8E0-event-if00 -> ../event18
lrwxrwxrwx 1 root root 10 Jul 30 18:10 usb-046d_0821_E259E9A0-event-if02 -> ../event19
lrwxrwxrwx 1 root root 10 Jul 31 03:21 usb-046d_HD_Pro_Webcam_C920_FC4DFDDF-event-if00 -> ../event15
lrwxrwxrwx 1 root root 10 Jul 30 18:10 usb-Logitech_USB_Receiver-if02-event-kbd -> ../event17
lrwxrwxrwx 1 root root 10 Jul 30 18:10 usb-Logitech_USB_Receiver-if02-event-mouse -> ../event16
lrwxrwxrwx 1 root root  9 Jul 30 18:10 usb-Logitech_USB_Receiver-if02-mouse -> ../mouse0

and find my keyboard, here it points to ../event17, i.e. /dev/input/event17,
but this can change on each reboot.  

