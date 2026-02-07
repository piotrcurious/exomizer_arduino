import os
import subprocess
import time
import sys

def generate_test_data():
    if not os.path.exists("test_data"):
        os.makedirs("test_data")

    with open("test_data/repetitive.txt", "w") as f:
        f.write("Exomizer " * 1000)

    with open("test_data/random.bin", "wb") as f:
        f.write(os.urandom(5000))

    with open("test_data/data.json", "w") as f:
        f.write('{"items": [' + ', '.join([str(i) for i in range(1000)]) + ']}')

def run_test(filename, preset="balanced"):
    print(f"Testing {filename} with preset {preset}...")

    input_path = os.path.join("test_data", filename)
    crunched_path = os.path.join("test_data", filename + ".exo")
    output_path = os.path.join("test_data", filename + ".out")

    # 1. Compress
    subprocess.run(["python3", "tools/exomizer_simple_compress.py", input_path, crunched_path, "--preset", preset], check=True)

    # 2. Decompress
    result = subprocess.run(["./tests/test_runner", crunched_path, output_path], capture_output=True, text=True)

    if result.returncode != 0:
        print(f"  FAILED: Decompression failed for {filename}")
        return False

    # 3. Verify
    with open(input_path, "rb") as f1, open(output_path, "rb") as f2:
        if f1.read() == f2.read():
            in_size = os.path.getsize(input_path)
            out_size = os.path.getsize(crunched_path)
            ratio = (out_size / in_size) * 100 if in_size > 0 else 0
            print(f"  SUCCESS: {in_size} -> {out_size} bytes ({ratio:.2f}%)")
            return True
        else:
            print(f"  FAILED: Data mismatch for {filename}")
            return False

def test_header_generation():
    print("Testing header generation...")
    input_path = "test_data/repetitive.txt"
    header_path = "test_data/test_header.h"

    subprocess.run(["python3", "tools/exomizer_simple_compress.py", input_path, header_path, "--name", "test_data"], check=True)

    if not os.path.exists(header_path):
        print("  FAILED: Header file not generated")
        return False

    # Verify header content (basic check)
    with open(header_path, "r") as f:
        content = f.read()
        if "test_data[]" in content and "test_data_len" in content:
            print("  SUCCESS: Header file looks correct")
            return True
        else:
            print("  FAILED: Header file content invalid")
            return False

def main():
    # Build test runner
    print("Building test runner...")
    subprocess.run(["g++", "-O3", "-I.", "tests/test_runner.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_runner"], check=True)

    generate_test_data()

    test_files = ["repetitive.txt", "random.bin", "data.json"]
    presets = ["balanced", "speed", "ratio"]

    success_count = 0
    total_tests = len(test_files) * len(presets) + 1

    for preset in presets:
        for f in test_files:
            if run_test(f, preset):
                success_count += 1

    if test_header_generation():
        success_count += 1

    print(f"\nSummary: {success_count}/{total_tests} tests passed.")
    if success_count == total_tests:
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
