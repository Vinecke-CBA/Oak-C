#ifndef OAK_SHOUT_H
#define OAK_SHOUT_H

/* Prototype shared by the C definition (shout.c) and the generated program:
   `extern fn shout(msg: string)` emits `extern void shout(OakStr msg);`
   with `typedef const char *OakStr;`, so `const char *` here matches
   exactly. Keep the two spellings in sync (see C-INTEROP.md). */
void shout(const char *msg);

#endif