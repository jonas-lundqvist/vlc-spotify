About
=====
This plugin adds support for playing spotify tracks in VLC media player.
The code should be seen as a prototype and is known to work badly, I can't claim that it is "work in progress" since I don't really know if I will further develop it into something useful. Please fork! :)

Why?
====
I did this to play around with libspotify and VLC.

Prerequisites
=============
 * Spotify Preemium account
 * libspotify
 * libspotify api key
 * VLC headers and libraries

Installation
============
Get an api key from spotify and copy it to *src/appkey.c*.
View and edit the Makefile in the *src/* directory if necessary.
Type *make*.
Copy the *src/libspotify_plugin.so* to the vlc access plugins folder, possibly */usr/lib/vlc/plugins/access*

Usage
=====
Start VLC and find the spotify preferences and set your username/password.

Start from gui:
File -> Open Network Stream -> spotify://spotify:track:6wNTqBF2Y69KG9EPyj9YJD -> Play

Or from command line:
vlc spotify://spotify:track:6wNTqBF2Y69KG9EPyj9YJD

License
=======
GNU LGPL 2.1. See the file *LICENSE*.
