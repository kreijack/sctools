#
# sctools
# Copyright (C) 2016 Tim Hentenaar.
#
# This code is licenced under the Simplified BSD License.
# See the LICENSE file for details.
#

EXTRA_DIST = README.md LICENSE m4 configs data

if UDEV
udevdir   = $(sysconfdir)/udev/rules.d
udev_DATA = udev/99-soarers-converter.rules
endif

SUBDIRS = $(HIDAPI_SUBDIR) src
ACLOCAL_AMFLAGS = -I m4

