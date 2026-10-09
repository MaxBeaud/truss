ROOT := justfile_directory()
PKG_ROOT := "/home/maximeb/Documents/libs"

# Configure + build truss itself (xmake bootstrap).
build mode="debug":
    xmake f -m {{mode}}
    xmake -y

# Build the example's build script, then run it end-to-end.
hello: build
    xmake -y hello-truss
    cd {{ROOT}}/examples/hello && ./truss debug && ./build/debug/bin/*/app

# Run truss's unit tests + the examples' tests through truss's `test` task.
test: build
    xmake -y truss-tests
    xmake run truss-tests
    xmake -y hello-truss shared-truss modules-truss fetch-truss
    cd {{ROOT}}/examples/hello && ./truss test
    cd {{ROOT}}/examples/shared && ./truss test
    cd {{ROOT}}/examples/modules && ./truss test
    cd {{ROOT}}/examples/fetch && ./truss test

clean:
    rm -rf {{ROOT}}/build/ {{ROOT}}/.xmake/
    cd {{ROOT}}/examples/hello && rm -rf build/ truss
    cd {{ROOT}}/examples/shared && rm -rf build/ truss
    cd {{ROOT}}/examples/modules && rm -rf build/ truss
    cd {{ROOT}}/examples/fetch && rm -rf build/ truss truss.lock

add-packages:
    #!/usr/bin/env bash
    packages=$(ls -1 "{{PKG_ROOT}}" | fzf --multi --prompt="Select packages: ")
    if [ -z "$packages" ]; then
        echo "No packages selected."
        exit 1
    fi
    while IFS= read -r package; do
        just add-package "$package"
    done <<< "$packages"

add-package package:
    #!/usr/bin/env bash
    if [ ! -d vendor ]; then mkdir vendor; fi
    if [ -d "{{PKG_ROOT}}/{{package}}" ]; then
        ln -sf "{{PKG_ROOT}}/{{package}}" vendor/{{package}}
        echo "Successfully linked {{package}}."
    else
        echo "Error: Package '{{package}}' not found!"
        echo "Did you mean one of these?"
        ls -1 "{{PKG_ROOT}}" | rg -i "{{package}}" || echo "  (No similar packages found)"
        exit 1
    fi
