import os
import subprocess
import time
import sys

def generate_test_data():
    if not os.path.exists("test_data/diverse"):
        os.makedirs("test_data/diverse")

    if not os.path.exists("test_data/diverse/code.cpp"):
        with open("test_data/diverse/code.cpp", "w") as f:
            f.write("// Sample code for testing\n")
            for i in range(100):
                f.write(f"void function_{i}() {{ int x = {i}; }}\n")

    with open("test_data/repetitive.txt", "w") as f:
        f.write("Exomizer Streaming Test " * 200)

    with open("test_data/random.bin", "wb") as f:
        f.write(os.urandom(1000))

    with open("test_data/data.json", "w") as f:
        f.write('{"items": [' + ', '.join([str(i) for i in range(200)]) + ']}')

def run_test(filename, preset="balanced", mode="block", window_size=32768, compressor="cpp"):
    mode_str = f"{mode} mode"
    if mode == "streaming":
        mode_str += f" (window: {window_size})"
    print(f"Testing {filename} with preset {preset} ({compressor} compressor) in {mode_str}...")

    input_path = filename
    if not os.path.exists(input_path):
        input_path = os.path.join("test_data", filename)
    if not os.path.exists(input_path):
        input_path = os.path.join("test_data/diverse", filename)

    crunched_path = "temp.exo"
    output_path = "temp.out"

    if compressor == "python":
        subprocess.run([sys.executable, "tools/exomizer_simple_compress.py", input_path, crunched_path, "--preset", preset], check=True, capture_output=True)
    else:
        subprocess.run(["./tools/exomizer_compress", input_path, crunched_path, preset], check=True, capture_output=True)

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
    print("Building project...")
    subprocess.run(["make", "clean"], check=True)
    subprocess.run(["make"], check=True)

    generate_test_data()

    success_count = 0
    tests = [
        ("repetitive.txt", "balanced", "block", 32768, "python"),
        ("repetitive.txt", "speed", "streaming", 32768, "cpp"),
        ("random.bin", "ratio", "block", 32768, "cpp"),
        ("data.json", "balanced", "streaming", 32768, "python"),
        ("code.cpp", "ratio", "streaming", 65536, "cpp"),
        ("test_fies/Prometheus.txt", "ratio", "streaming", 65536, "cpp"),
        ("test_fies/Prometheus.txt", "ratio", "block", 128000, "cpp"),
    ]

    for filename, preset, mode, window, compressor in tests:
        if run_test(filename, preset, mode, window, compressor):
            success_count += 1

    print(f"\nSummary: {success_count}/{len(tests)} tests passed.")

    # Cleanup
    if os.path.exists("temp.exo"): os.remove("temp.exo")
    if os.path.exists("temp.out"): os.remove("temp.out")

    if success_count == len(tests):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
