#
# exempi - Makefile.am
#
# Copyright (C) 2007-2013 Hubert Figuiere
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1 Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# 
# 2 Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the
# distribution.
#
# 3 Neither the name of the Authors, nor the names of its
# contributors may be used to endorse or promote products derived
# from this software wit hout specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
#


noinst_LTLIBRARIES = libpluginhandler.la

AM_CXXFLAGS = -Wno-multichar -Wno-ctor-dtor-privacy \
	-funsigned-char -fexceptions -fno-strict-aliasing
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/public/include \
	-I$(top_srcdir)/XMPFilesPlugins/api/source/ \
	-Wall @XMPCORE_CPPFLAGS@

if MAC_ENV
PLATFORM_UTILS=OS_Utils_Mac.cpp
else
PLATFORM_UTILS=OS_Utils_Linux.cpp
endif

libpluginhandler_la_SOURCES = FileHandler.h \
	FileHandlerInstance.cpp FileHandlerInstance.h \
	HostAPIImpl.cpp \
	Module.cpp Module.h ModuleUtils.h \
	PluginManager.cpp PluginManager.h \
	XMPAtoms.cpp XMPAtoms.h \
	$(PLATFORM_UTILS) \
	$(NULL)

EXTRA_DIST=OS_Utils_Linux.cpp OS_Utils_WIN.cpp OS_Utils_Mac.cpp
