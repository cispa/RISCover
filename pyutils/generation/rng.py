#!/usr/bin/env python3

class SharedRng:
    def __init__(self, seed):
        self.index = 624
        self.mt = [0] * 624
        self._seed_mt(seed)

    def _seed_mt(self, seed):
        self.mt[0] = seed & 0xFFFFFFFF
        for i in range(1, 624):
            self.mt[i] = (1812433253 * (self.mt[i - 1] ^ (self.mt[i - 1] >> 30)) + i) & 0xFFFFFFFF

    def _twist(self):
        for i in range(624):
            y = (self.mt[i] & 0x80000000) + (self.mt[(i + 1) % 624] & 0x7FFFFFFF)
            self.mt[i] = self.mt[(i + 397) % 624] ^ (y >> 1)
            if y % 2 != 0:
                self.mt[i] ^= 0x9908B0DF
        self.index = 0

    def _extract_number(self):
        if self.index >= 624:
            self._twist()

        y = self.mt[self.index]
        self.index += 1

        # Tempering
        y ^= (y >> 11)
        y ^= (y << 7) & 0x9D2C5680
        y ^= (y << 15) & 0xEFC60000
        y ^= (y >> 18)

        return y & 0xFFFFFFFFFFFFFFFF  # force to 64-bit result for compatibility

    def next(self):
        return self._extract_number()

    def randint(self, a, b):
        return a + (self.next() % (b - a + 1))

    def custom_choices(self, sequence, k):
        return [sequence[self.randint(0, len(sequence) - 1)] for _ in range(k)]

if __name__ == '__main__':
    rng = SharedRng(101)
    rng2 = SharedRng(100)
    rng3 = SharedRng(101)
    a = [rng3.randint(0,2**32) for i in range(1000000)]
    print([hex(i) for i in reversed(sorted(a))])
    print(len(set(a)))
    print(list(hex(rng.randint(0, 7)) for _ in range(10)))
    print(list(hex(rng2.randint(0, 7)) for _ in range(10)))
    print(list(hex(rng.randint(0, 1<<32)) for _ in range(10)))
    print(list(hex(rng2.randint(0, 1<<32)) for _ in range(10)))
