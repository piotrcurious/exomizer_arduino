import os
import subprocess
import time
import random
import sys

def generate_test_data():
    if not os.path.exists("test_data"):
        os.makedirs("test_data")

    # 1. Repetitive text
    with open("test_data/repetitive.txt", "w") as f:
        f.write("Exomizer " * 1000)

    # 2. Random binary
    with open("test_data/random.bin", "wb") as f:
        f.write(os.urandom(5000))

    # 3. Structured text (JSON)
    with open("test_data/data.json", "w") as f:
        f.write('{"items": [' + ', '.join([str(i) for i in range(1000)]) + ']}')

    # 4. Small "image" (structured binary)
    with open("test_data/image.raw", "wb") as f:
        for y in range(64):
            for x in range(64):
                f.write(bytes([ (x ^ y) & 0xFF ]))

def run_test(filename):
    print(f"Testing {filename}...")

    input_path = os.path.join("test_data", filename)
    crunched_path = os.path.join("test_data", filename + ".exo")
    output_path = os.path.join("test_data", filename + ".out")

    # 1. Compress
    start_comp = time.time()
    subprocess.run(["python3", "tools/exomizer_simple_compress.py", input_path, crunched_path], check=True)
    end_comp = time.time()

    # 2. Decompress
    start_decomp = time.time()
    result = subprocess.run(["./tests/test_runner", crunched_path, output_path], capture_output=True, text=True)
    end_decomp = time.time()

    if result.returncode != 0:
        print(f"  FAILED: Decompression failed for {filename}")
        print(result.stderr)
        return False

    # 3. Verify
    with open(input_path, "rb") as f1, open(output_path, "rb") as f2:
        if f1.read() == f2.read():
            in_size = os.path.getsize(input_path)
            out_size = os.path.getsize(crunched_path)
            ratio = (out_size / in_size) * 100 if in_size > 0 else 0
            print(f"  SUCCESS: {in_size} -> {out_size} bytes ({ratio:.2f}%)")
            print(f"  {result.stdout.strip()}")
            return True
        else:
            print(f"  FAILED: Data mismatch for {filename}")
            return False

def main():
    # Build test runner
    print("Building test runner...")
    subprocess.run(["g++", "-O3", "-I.", "tests/test_runner.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_runner"], check=True)

    generate_test_data()

    test_files = [f for f in os.listdir("test_data") if not f.endswith(".exo") and not f.endswith(".out")]

    success_count = 0
    for f in test_files:
        if run_test(f):
            success_count += 1

    print(f"\nSummary: {success_count}/{len(test_files)} tests passed.")
    if success_count == len(test_files):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
