import os
import subprocess
import sys
import hashlib

def run_command(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Command failed: {' '.join(cmd)}")
        print(result.stderr)
        return False, result.stderr
    return True, result.stdout

def test_file(filepath, preset, mode, window_size=32768):
    input_file = filepath
    compressed_file = filepath + ".exo"
    decompressed_file = filepath + ".out"

    # Compress
    success, output = run_command([sys.executable, "tools/exomizer_simple_compress.py", input_file, compressed_file, "--preset", preset])
    if not success: return False

    # Decompress
    if mode == "block":
        success, output = run_command(["./tests/test_runner", compressed_file, decompressed_file])
    else:
        success, output = run_command(["./tests/test_streaming", compressed_file, decompressed_file, str(window_size)])

    if not success: return False

    # Compare
    with open(input_file, "rb") as f: input_data = f.read()
    if os.path.exists(decompressed_file):
        with open(decompressed_file, "rb") as f: decompressed_data = f.read()
    else:
        print(f"Decompressed file {decompressed_file} not found")
        return False

    if input_data == decompressed_data:
        in_size = len(input_data)
        out_size = os.path.getsize(compressed_file)
        ratio = (out_size / in_size * 100) if in_size > 0 else 0
        print(f"  [PASS] {filepath} ({preset}, {mode}): {in_size} -> {out_size} ({ratio:.2f}%)")
        return True
    else:
        print(f"  [FAIL] {filepath} ({preset}, {mode}): Data mismatch!")
        return False

def main():
    # Build runners
    print("Building runners...")
    run_command(["g++", "-O3", "-I.", "tests/test_runner.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_runner"])
    run_command(["g++", "-O3", "-I.", "tests/test_streaming.cpp", "src/exomizer_decompress.cpp", "-o", "tests/test_streaming"])

    files = [
        "test_data/diverse/code.cpp",
        "test_data/diverse/random_5k.bin",
        "test_data/diverse/one_byte.bin",
        "test_data/diverse/empty.bin",
        "test_data/diverse/digits_repetitive.txt"
    ]

    all_passed = True
    for f in files:
        for preset in ["speed", "balanced", "ratio"]:
            for mode in ["block", "streaming"]:
                if not test_file(f, preset, mode):
                    all_passed = False

    if all_passed:
        print("\nAll comprehensive tests passed!")
        sys.exit(0)
    else:
        print("\nSome tests failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()
