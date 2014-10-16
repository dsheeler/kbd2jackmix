#! /usr/bin/perl

my $dir = "/dev/input/by-id";
opendir DIR, $dir or die "couldn't open '$dir': $!";
my @kbd_files = grep { /kbd/ } readdir(DIR);
closedir(DIR);
my $file = "$dir/$kbd_files[0]";
system("sudo chmod 770 $file");
system("sudo chgrp audio $file");
system("kbd2jackmix $file");


