#define WIN32_LEAN_AND_MEAN

#ifndef _MAIN_H
#define _MAIN_H

#undef SHADER_DEBUG //define this for shader debugging logs

char *GetDirectoryFile(char *filename);
void __cdecl add_log (const char * fmt, ...);

#endif