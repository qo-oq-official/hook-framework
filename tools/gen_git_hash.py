import sys
import os

def main():
    if len(sys.argv) < 3:
        print("Usage: gen_git_hash.py <status_file> <output_file>")
        sys.exit(1)

    status_file = sys.argv[1]
    output_file = sys.argv[2]
    git_hash = "unknown"

    # 從 Bazel 的狀態文件中提取 STABLE_GIT_HASH
    if os.path.exists(status_file):
        with open(status_file, "r") as f:
            for line in f:
                if line.startswith("STABLE_GIT_HASH"):
                    parts = line.strip().split(" ", 1)
                    if len(parts) == 2:
                        git_hash = parts[1]

    # 確保輸出目錄存在
    os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)

    # 寫入跨平台的頭文件內容
    with open(output_file, "w") as f:
        f.write("#ifndef GIT_HASH_H\n")
        f.write("#define GIT_HASH_H\n\n")
        f.write(f'#define HOOKFW_GIT_HASH "{git_hash}"\n\n')
        f.write("#endif // GIT_HASH_H\n")

if __name__ == "__main__":
    main()
