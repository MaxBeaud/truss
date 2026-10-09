#pragma once
struct buffer;
struct edge;

void depsinit(const char *);
void depsclose(void);
void depsload(struct edge *);
void depsrecord(struct edge *);
/* deps = msvc: parse /showIncludes lines out of a finished job's captured
 * stdout (also stripping them from the buffer) and record them */
void depsrecord_msvc(struct edge *, struct buffer *);
