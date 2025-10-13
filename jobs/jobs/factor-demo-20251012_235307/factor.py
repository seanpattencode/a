import sys
def factors(n):
    return [i for i in range(1, n + 1) if n % i == 0]
if __name__ == "__main__":
    print(factors(int(sys.argv[1])))
