#!/usr/bin/env python3
from abc import ABC, abstractmethod
from multiprocessing import Process, Value, Condition, Manager
from time import time, sleep
import sys

from pyutils.inp import Input
from pyutils.util import sec_to_str
from pyutils.server import Server, Client

# TODO: handling of keyboard interrupt
class FuzzServer(ABC):
    WAIT = 9
    INTERVAL = 1

    def __init__(self, server: Server, clients: list[Client], additional_worker_args, worker_class, single_step, start_counter_at=0):
        self.server = server
        self.clients = clients
        self.additional_worker_args = additional_worker_args
        self.worker_class = worker_class
        self.single_step = single_step

        self.condition = Condition()
        self.executed_counter = Value('Q', start_counter_at)
        self.finished = Value('b', False)

        # TODO: should we use manager for all of them?
        # this one doesn't work without manager
        self.manager = Manager()
        self.status_line = self.manager.Value('c', f'Status line will appear here after {self.WAIT+self.INTERVAL}s')

    @abstractmethod
    def stats(self, elapsed_seconds: int) -> list[tuple[str, str]]:
        pass

    def start(self):
        # clients = server.clients
        self.print("n clients:", len(self.clients))
        self.print("clients:", ", ".join(map(str, self.clients)))

        workers = [self.worker_class(self, self.clients, i, *self.additional_worker_args) for i in range(50 if not self.single_step else 1)]

        for worker in workers:
            worker.start()

        # Sleep so that stats are somewhat correct at the start
        sleep(self.WAIT)
        with self.condition:
            start_executed_counter = self.executed_counter.value
        start_time = time()
        while True:
            sleep(self.INTERVAL)

            with self.condition:
                executed_counter = self.executed_counter.value
            elapsed_seconds = int(time() - start_time)

            elapsed_exec_counter = executed_counter-start_executed_counter
            executed_counter_per_sec = elapsed_exec_counter // elapsed_seconds

            status = [
                ("Counter", str(executed_counter)),
                ("Elapsed", sec_to_str(elapsed_seconds)),
                ("Inp/s", str(executed_counter_per_sec))
            ] + self.stats(elapsed_seconds)

            with self.condition:
                self.status_line.value = " | ".join([a+": "+b for (a, b) in status])
                self.print_status()

            with self.condition:
                if self.finished.value:
                    break

        for worker in workers:
            worker.join()

    def real_print(self, *args, **kwargs):
        # Clear the old status line
        sys.stdout.write("\033[K")
        print(*args, **kwargs)

    def print_status(self):
        self.real_print(self.status_line.value, end='\r', flush=True)

    def print(self, *args, **kwargs):
        # In the condition to make sure that output is not interleaved
        with self.condition:
            self.real_print(*args, **kwargs)
            # Reprint the status line
            self.print_status()

# Process that generates inputs, distributes them, collects them back and analyzes them in some form.
class Worker(ABC, Process):
    def __init__(self, fuzz_server: FuzzServer, clients: list[Client], n: int):
        super().__init__()
        self.fuzz_server = fuzz_server
        self.clients = clients
        self.n = n

    @abstractmethod
    def run(self):
        pass
