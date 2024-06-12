import os
import subprocess
import sys

def main(argv):
    output_file = argv[1]
    generate_version_exe = os.path.join(os.path.dirname(__file__), "generate_version")
    
    result = subprocess.run(generate_version_exe, capture_output=True, text=True)
    
    if result.returncode != 0:
        print("Error generating version header", file=sys.stderr)
        return 1
    
    with open(output_file, 'w') as f:
        f.write(result.stdout)
    
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
