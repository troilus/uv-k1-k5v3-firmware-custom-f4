import sys

def load(p):
    return [l.split(":", 1)[1].strip() for l in open(p) if ":" in l]

a = load(sys.argv[1])
b = load(sys.argv[2])
n = min(len(a), len(b))
print("len", len(a), len(b))
diff = [i for i in range(n) if a[i] != b[i]]
print("ndiff", len(diff), "first", diff[:20])
print("A:", "".join(a[:64]))
print("B:", "".join(b[:64]))
# expected training + flag
exp = "".join("01" for _ in range(12)) + "01111110"
print("E:", exp)
