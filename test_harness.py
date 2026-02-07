import os
import subprocess
import time
import sys

def generate_test_data():
    if not os.path.exists("test_data"):
        os.makedirs("test_data")

    with open("test_data/repetitive.txt", "w") as f:
        f.write("Exomizer Streaming Test " * 200)

    with open("test_data/random.bin", "wb") as f:
        f.write(os.urandom(1000))

    with open("test_data/data.json", "w") as f:
        f.write('{"items": [' + ', '.join([str(i) for i in range(200)]) + ']}')

    with open("test_data/large_repetitive.txt", "w") as f:
        for i in range(100):
            f.write(f"Line {i:03}: Repetitive pattern. ")

def run_test(filename, preset="balanced", mode="block", window_size=32768):
    mode_str = f"{mode} mode"
    if mode == "streaming":
        mode_str += f" (window: {window_size})"
    print(f"Testing {filename} with preset {preset} in {mode_str}...")

    input_path = os.path.join("test_data", filename)
    crunched_path = os.path.join("test_data", filename + ".exo")
    output_path = os.path.join("test_data", filename + ".out")

    subprocess.run([sys.executable, "tools/exomizer_simple_compress.py", input_path, crunched_path, "--preset", preset], check=True)

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
    print("Building test runners...")
    subprocess.run(["g++", "-O3", "-I.", "tests/test_runner.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_runner"], check=True)
    subprocess.run(["g++", "-O3", "-I.", "tests/test_streaming.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_streaming"], check=True)

    generate_test_data()

    success_count = 0
    tests = [
        ("repetitive.txt", "balanced", "block", 32768),
        ("repetitive.txt", "speed", "streaming", 32768),
        ("random.bin", "ratio", "block", 32768),
        ("data.json", "balanced", "streaming", 32768),
        ("large_repetitive.txt", "ratio", "streaming", 4096),
    ]

    for filename, preset, mode, window in tests:
        if run_test(filename, preset, mode, window):
            success_count += 1

    print(f"\nSummary: {success_count}/{len(tests)} tests passed.")

    # Cleanup binaries after test
    if os.path.exists("tests/test_runner"): os.remove("tests/test_runner")
    if os.path.exists("tests/test_streaming"): os.remove("tests/test_streaming")

    if success_count == len(tests):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
