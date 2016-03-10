#
# sctools
# Copyright (C) 2016 Tim Hentenaar.
#
# This code is licenced under the Simplified BSD License.
# See the LICENSE file for details.
#

noinst_HEADERS = hid_tokens.h macro_tokens.h token.h rawhid_defs.h commands.h
bin_PROGRAMS   = scas scdis sctool

scas_SOURCES   = scas.c hid_tokens.c macro_tokens.c
scdis_SOURCES  = scdis.c hid_tokens.c macro_tokens.c
sctool_SOURCES = sctool.c commands.c hid_tokens.c

if BUILD_HIDAPI
sctool_CPPFLAGS  = -I$(top_srcdir)/hidapi
sctool_LDADD    = $(top_srcdir)/hidapi/$(HIDAPI_OS)/libhidapi$(HIDAPI_TARGET).la
else
sctool_LDADD     = -lhidapi$(HIDAPI_TARGET)
endif
