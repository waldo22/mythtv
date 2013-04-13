R5000 for MythTV 0.26
=====================

This is the full source of MythTV 0.26, pre-patched with support for the R5000-HD STB modification for your convenience.

This repository is almost entirely based on Alan Nisota's fine work on his R5000 for Linux code, which patched MythTV versions from 0.22 through 0.24.

Many of Alan's files were used directly without modification, so thanks to him for making this possible.

Other changes made by me were all based on the existing support in MythTV for IPTV tuners and Firewire tuners, as Alan's original changes were, and you'll notice that the R5000 code is very similar to the code for these "tuners".

You should be able to load the R5000 firmware, compile, install and enjoy.

Compiling is standard stuff.

./configure --enable-proc-opt
make -j 2
make install

Feedback is welcomed!


To compile on Debian, the following libs should be installed:

libusb-dev libgdb-dev qt4-qmake yasm uuid-dev libfreetype6-dev zlib1g-dev libmp3lame-dev libqtwebkit-dev libxxf86vm-dev x11proto-xf86vidmode-dev libxinerama-dev libxinerama1 x11proto-xinerama-dev mysql-server pkg-config python-mysqldb libxslt1.1 python-lxml python-pycurl python-urlgrabber libsocket6-perl libio-socket-inet6-perl libnet-upnp-perl fxload
