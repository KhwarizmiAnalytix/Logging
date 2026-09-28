# Find Package Example

This example demonstrates how to use the installed Logging library via `find_package()`.

## Building the Example

### Option 1: Using an installed Logging package

If Logging is already installed on your system:

```bash
cd Examples/FindPackageExample
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/logging/install
cmake --build build
ctest --test-dir build --output-on-failure
```

### Option 2: Build and install Logging first, then test

```bash
# From the Logging repository root
cd Scripts
python3 setup.py install --backend=native

# Now build the example
cd ../Examples/FindPackageExample
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/logging/install
cmake --build build
./build/example_app
```

## What This Example Tests

- ✅ Installation of Logging package
- ✅ CMake `find_package()` integration
- ✅ Linking against installed library
- ✅ Using public headers from installed location
- ✅ Runtime execution of linked application

## Key Files

- `CMakeLists.txt` - Build configuration using `find_package(Logging)`
- `main.cpp` - Example application demonstrating Logging API usage

## Expected Output

When run successfully, the application will output structured log messages
at different verbosity levels using the installed Logging library.
