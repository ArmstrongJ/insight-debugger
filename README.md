insight-debugger
================

A forked Insight Debugger (GDB + Tcl/Tk Interface)

This codebase is based on an official Insight CVS April 28, 2012 checkout.  The official project is maintained 
at http://sources.redhat.com/insight/.  The main goal of this fork is to allow reliable builds and operation
on Microsoft Windows platforms, both 32-bit and 64-bit, although other systems should continue to work.  

Differences from Official
-------------------------

The most noticable difference is the absence of Tcl/Tk within the repository.  The version currently shipped
with official Insight is outdated and cannot be built on Windows 64-bit.  The developer must have a functional
Tcl/Tk installation with both incr tcl and incr tk.  Other differences include:

 * Minor look-and-feel font adjustments
 * Standard mouse cursor
 * Fixes related to Fortran debugging
 
This codebase is the version shipped with the [Simply Fortran](http://simplyfortran.com/) integrated 
development environment.

Building on Windows
-------------------

To build on Windows systems, you'll need to install MinGW and MSYS.  The build may work on Cygwin, but it
will most likely produce Cygwin-dependent code.  As stated already, a working Tcl/Tk with incr tcl and incr tk
must be installed with full development headers.  

If the requirements are met, you can simply proceed with:

$ ./configure --prefix=<path to install> --with-tcl=<tcl-directory> --with-tk=<tk-directory> 
$ make
$ make install


