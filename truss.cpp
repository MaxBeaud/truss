// truss's own build script (self-hosting).
//
// truss building truss with C++ modules arrives with PLAN.MD phase 7; until
// then this script validates the consumption path end to end: `import truss;`
// compiled against libtruss.a, producing ./truss.
import truss;

int main(int argc, char** argv) {
    truss::Project proj("truss", argc, argv);

    // Placeholder: real self-host target definitions go here once truss can
    // compile module interface units for user projects (phase 4/7).
    auto result = proj.build();
    return result ? 0 : 1;
}
