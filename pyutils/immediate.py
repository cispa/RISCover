#!/usr/bin/env python3
import random
from abc import ABC, abstractmethod

class Immediate(ABC):
    def __init__(self, ranges):
        self.bits = 0
        for msb, lsb in ranges:
            self.bits+=msb-lsb+1
        self.ranges = sorted(ranges)

    def get_random(self):
        r = random.choice(self.nums)
        if r != None:
            return r

        # Pick a small immediate in 90% of the random cases
        if random.randint(1, 10) <= 9:
            return self.small_immediate()
        else:
            return random.getrandbits(self.bits)

    def bit_mask(self, num):
        mask=0
        bit=0
        # NOTE: ranges are sorted
        # TODO: ???
        # TODO: ???
        # TODO: ???
        # TODO: ???
        for msb, lsb in self.ranges:
            bits = msb-lsb+1
            mask |= ((2**bits-1) & (num >> bit)) << lsb
            bit += bits
        return mask

    def get_random_mask(self):
        return self.bit_mask(self.get_random())

class SignedImmediate(Immediate):
    def __init__(self, ranges):
        super().__init__(ranges)

        self.max = 2**(self.bits-1)-1
        self.min = -2**(self.bits-1)
        # TODO: too many values here restricts from
        # finding bugs where the memory address is needed
        self.nums = [
            self.twos(-1),
            self.twos(0),
            # self.twos(1),
            # self.twos(42),
            # self.twos(1337),
            self.twos(self.min),
            self.twos(self.max),

            # None generates a random bits number
            None
        ]

    def twos(self, n):
        if n < self.min or n > self.max:
            raise ValueError(f"Number out of range for {n}-bit two's complement representation")

        if n < 0:
            return (1<<self.bits) + n
        else:
            return n

    def small_immediate(self):
        return self.twos(random.randint(max(self.min, -8), min(self.max, 8)))

class UnsignedImmediate(Immediate):
    def __init__(self, ranges):
        super().__init__(ranges)

        self.max = 2**(self.bits)-1
        self.min = 0
        self.nums = [
            self.min,
            self.max,

            # None generates a random bits number
            None
        ]

    def small_immediate(self):
        return random.randint(0, max(self.max, 16))
