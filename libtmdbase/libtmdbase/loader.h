/***************************************************************************
                          loader.h  -  description
                             -------------------
    begin                : Mon Mar 10 2003
    copyright            : (C) 2003 by Heming Ling
    email                : 
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
 #include "dlfcn.h"
 
 #define XRTP_DLFLAGS RTLD_LAZY
 
 void* modu_dlopen(char *fn, int flag);
 int modu_dlclose(void * lib);
 void* modu_dlsym(void *h, char *name);
 const char * modu_dlerror(void);
