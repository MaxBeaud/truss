/* truss shim: drives the vendored samurai (vendor/samurai) as a library.
 *
 * This replaces samu.c's main() driver. It must stay in sync with the tail of
 * samu.c's main() (see vendor/samurai/TRUSS-VENDOR.txt). Upstream samurai is
 * never modified; everything samurai-specific in truss goes through this file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "build.h"
#include "deps.h"
#include "env.h"
#include "graph.h"
#include "log.h"
#include "os.h"
#include "parse.h"
#include "tool.h"
#include "util.h"

/* samurai's warn()/fatal() print this; samu.c defines it, but we exclude samu.c */
const char *argv0 = "truss-samu";

/* samurai intentionally never frees its graph/log/deps state (it is sized for
 * the process lifetime). When truss is built with ASan, silence LSan. This
 * hook is inert when ASan is not linked in. */
const char *__lsan_default_options(void)
{
	return "detect_leaks=0";
}

/* Keep in sync with struct truss_samu_options in src/core/samu.cpp. */
struct truss_samu_options {
	const char *manifest;
	const char *statusfmt; /* NULL => "[%s/%t] " */
	long maxjobs;          /* <= 0 => auto (nproc + 2) */
	long maxfail;          /* 0 => 1, < 0 => unlimited */
	int verbose, dryrun, explain;
	int ntargets;
	const char *const *targets; /* explicit nodes; empty => default nodes */
};

static char *
getbuilddir(void)
{
	struct string *builddir;

	builddir = envvar(rootenv, "builddir");
	if (!builddir)
		return NULL;
	if (osmkdirs(builddir, false) < 0)
		exit(1);
	return builddir->s;
}

static void
set_default_jobs(void)
{
	long nproc;

	if (buildopts.maxjobs)
		return;
	nproc = osnproc();
	switch (nproc) {
	case -1: case 0: case 1:
		buildopts.maxjobs = 2;
		break;
	case 2:
		buildopts.maxjobs = 3;
		break;
	default:
		buildopts.maxjobs = nproc + 2;
		break;
	}
}

int
truss_samu_build(const struct truss_samu_options *o)
{
	char *builddir;
	struct node *n;
	int tries = 0, i;

	buildopts.maxfail = o->maxfail == 0 ? 1 : (o->maxfail < 0 ? (size_t)-1 : (size_t)o->maxfail);
	buildopts.verbose = o->verbose;
	buildopts.dryrun = o->dryrun;
	buildopts.explain = o->explain;
	buildopts.statusfmt = o->statusfmt ? o->statusfmt : "[%s/%t] ";
	buildopts.maxjobs = o->maxjobs > 0 ? (size_t)o->maxjobs : 0;
	set_default_jobs();

	setvbuf(stdout, NULL, _IOLBF, 0);

retry:
	/* (re-)initialize global graph, environment, and parse structures */
	graphinit();
	envinit();
	parseinit();

	/* parse the manifest */
	parse(o->manifest, rootenv);

	/* load the build log */
	builddir = getbuilddir();
	loginit(builddir);
	depsinit(builddir);

	/* rebuild the manifest if it's dirty */
	n = nodeget(o->manifest, 0);
	if (n && n->gen) {
		buildadd(n);
		if (n->dirty) {
			build();
			if (n->gen->flags & FLAG_DIRTY_OUT || n->gen->nprune > 0) {
				if (++tries > 100)
					fatal("manifest '%s' dirty after 100 tries", o->manifest);
				if (!buildopts.dryrun)
					goto retry;
			}
			/* manifest was pruned; reset state, then continue with build */
			buildreset();
		}
	}

	/* finally, build any specified targets or the default targets */
	if (o->ntargets > 0) {
		for (i = 0; i < o->ntargets; ++i) {
			n = nodeget(o->targets[i], 0);
			if (!n)
				fatal("unknown target '%s'", o->targets[i]);
			buildadd(n);
		}
	} else {
		defaultnodes(buildadd);
	}
	build();

	logclose();
	depsclose();

	return 0;
}

int
truss_samu_compdb(const char *manifest, const char *outpath)
{
	FILE *f;
	int saved, rc;
	char *argv[] = {"compdb", "compile_cxx", "compile_c", "compile_bmi"};

	graphinit();
	envinit();
	parseinit();
	parse(manifest, rootenv);

	f = fopen(outpath, "w");
	if (!f)
		return -1;
	saved = dup(1);
	fflush(stdout);
	dup2(fileno(f), 1);
	rc = toolget("compdb")->run((int)(sizeof(argv) / sizeof(argv[0])), argv);
	fflush(stdout);
	dup2(saved, 1);
	close(saved);
	fclose(f);
	return rc;
}
