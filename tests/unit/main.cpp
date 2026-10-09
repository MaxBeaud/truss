/* doctest main TU — the only place the framework implementation is compiled.
 * Plain translation unit (no module): it needs no truss entities, the test
 * units register themselves into its registry at static-init time. */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
