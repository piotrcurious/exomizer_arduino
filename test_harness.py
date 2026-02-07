import os
import subprocess
import time
import sys

def generate_test_data():
    if not os.path.exists("test_data"):
        os.makedirs("test_data")

    with open("test_data/repetitive.txt", "w") as f:
        f.write("Exomizer Streaming Test " * 1000)

    with open("test_data/random.bin", "wb") as f:
        f.write(os.urandom(5000))

    with open("test_data/data.json", "w") as f:
        f.write('{"items": [' + ', '.join([str(i) for i in range(1000)]) + ']}')

    # Large repetitive file to test circular window
    with open("test_data/large_repetitive.txt", "w") as f:
        for i in range(1000):
            f.write(f"Line {i:04}: This is a repetitive line to test circular window buffer logic. ")

def run_test(filename, preset="balanced", mode="block", window_size=32768):
    mode_str = f"{mode} mode"
    if mode == "streaming":
        mode_str += f" (window: {window_size})"
    print(f"Testing {filename} with preset {preset} in {mode_str}...")

    input_path = os.path.join("test_data", filename)
    crunched_path = os.path.join("test_data", filename + ".exo")
    output_path = os.path.join("test_data", filename + ".out")

    # 1. Compress
    # Ensure window size used for compression doesn't exceed decompression window
    comp_window = min(window_size, 32767) # Our simple compressor currently caps at 32767
    subprocess.run(["python3", "tools/exomizer_simple_compress.py", input_path, crunched_path, "--preset", preset], check=True)

    # 2. Decompress
    if mode == "block":
        runner = "./tests/test_runner"
        args = [runner, crunched_path, output_path]
    else:
        runner = "./tests/test_streaming"
        args = [runner, crunched_path, output_path, str(window_size)]

    result = subprocess.run(args, capture_output=True, text=True)

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
            return True
        else:
            print(f"  FAILED: Data mismatch for {filename}")
            return False

def main():
    # Build test runners
    print("Building test runners...")
    subprocess.run(["g++", "-O3", "-I.", "tests/test_runner.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_runner"], check=True)
    subprocess.run(["g++", "-O3", "-I.", "tests/test_streaming.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_streaming"], check=True)

    generate_test_data()

    success_count = 0
    tests = [
        ("repetitive.txt", "balanced", "block", 32768),
        ("repetitive.txt", "balanced", "streaming", 32768),
        ("random.bin", "balanced", "block", 32768),
        ("random.bin", "balanced", "streaming", 32768),
        ("data.json", "balanced", "block", 32768),
        ("data.json", "balanced", "streaming", 32768),
        # Circular window test: large file with small window
        # The compressor uses balanced preset which has 32767 window.
        # But for large_repetitive.txt, it will use offsets.
        # If we decompress with a small window (e.g. 8KB), it should fail if offsets > 8KB.
        # So we should use a window at least as large as the compression window.
        ("large_repetitive.txt", "balanced", "block", 65536),
        ("large_repetitive.txt", "balanced", "streaming", 32768), # matches compression window
    ]

    for filename, preset, mode, window in tests:
        if run_test(filename, preset, mode, window):
            success_count += 1

    print(f"\nSummary: {success_count}/{len(tests)} tests passed.")
    if success_count == len(tests):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
