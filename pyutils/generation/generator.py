#!/usr/bin/env python3
from pyutils.inp import Input
from abc import ABC, abstractmethod
from pyutils.shared_logic import get_collection_from_args

class Generator(ABC):
    def __init__(self, instruction_collection, compress_send=True):
        self.instruction_collection = instruction_collection
        self.expand_inputs_after_exec = False
        self.compress_send = compress_send

    @abstractmethod
    def generate(self) -> list[Input]:
        pass

    def to_dict(self) -> dict:
        return {
            "type": self.__class__.__name__,
            "instruction_collection": self.instruction_collection.to_dict(),
        }

    @abstractmethod
    def parser_add_args(parser):
        pass

    def get_build_flags(self) -> tuple[set, set]:
        if self.compress_send:
            return set(), {"-DCOMPRESS_RECV"}
        else:
            return set(), set()

    def late_init(self, build_flags):
        pass

    def early_init(self, build_flags):
        pass


class DiffFuzzGenerator(Generator):
    @abstractmethod
    def generate(self, counter: int, n: int) -> list[Input]:
        pass
