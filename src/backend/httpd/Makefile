#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for src/backend/httpd
#
# IDENTIFICATION
#    src/backend/httpd/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/httpd
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

override CPPFLAGS := -I$(srcdir) $(CPPFLAGS)

OBJS = httplistener.o

include $(top_srcdir)/src/backend/common.mk
