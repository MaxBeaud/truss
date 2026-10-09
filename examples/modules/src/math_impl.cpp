// Implementation unit of module math: implicitly imports the primary interface
// (which export imports :part), so both add and mul are visible here.
module math;

namespace mathx {

int add(int a, int b) {
    return a + b;
}

int mul(int a, int b) {
    return a * b;
}

} // namespace mathx
