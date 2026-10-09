/* truss <-> samurai shim ABI. Included by src/samu/shim.c (as C) and by
 * src/core/samu.cpp (as C++, via the global module fragment). The extern "C"
 * guard also makes the definitions in shim.c pick up C linkage when shim.c is
 * compiled as C++ (single-TU amalgamation, dist/truss_all.hpp). */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

/* Keep in sync between shim.c usage and samu.cpp call sites. */
struct truss_samu_options {
	const char *manifest;
	const char *statusfmt; /* NULL => "[%s/%t] " */
	long maxjobs;          /* <= 0 => auto (nproc + 2) */
	long maxfail;          /* 0 => 1, < 0 => unlimited */
	int verbose, dryrun, explain;
	int ntargets;
	const char *const *targets;
};

int truss_samu_build(const struct truss_samu_options *o);
int truss_samu_compdb(const char *manifest, const char *outpath);

#if defined(__cplusplus)
}
#endif
