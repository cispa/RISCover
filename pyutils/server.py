import socket
from multiprocessing import Manager, Process, Condition, Lock, Value
import threading
from threading import Thread
from ctypes import c_wchar_p
import select
import sys
import struct
import zlib
from collections import defaultdict
from abc import ABC, abstractmethod

from pyutils.lscpu import CPUInfo, Microarchitecture, parse_lscpu_output
from pyutils.qemu_cpuinfo  import QemuCPUInfo
from pyutils.riscv_fake_cpuinfo  import RiscvFakeCPUInfo
from pyutils.cpuinfo import parse_cpuinfo
from pyutils.result import Result, parse_results
from pyutils.inp import pack_inputs, Input
from pyutils.util import client_to_header_colored, hardcoded_machine_info, qemu_config, serial_to_hostname, client_to_header, hardcoded_lscpu, raise_nofile_limit
import pyutils.config as config

# NOTE:
# lab32 + lab48 best performance:
# 3 processes, 12 in flight, up to 68k

# TODO: join c906 instances for undoc fuzz
# TODO: we probably have problems with multiple workers when state is loaded later, confirm this (in probably-more-clients-problems)
# TODO: next step is probably instruction triples
# TODO: server stdout currently does not contain current counter, but we still have reproducers and rnglog
# TODO: it seems like the current bottleneck is actually io, the cpu doesnt spin anymore,
# but logging the data available to the client reveals that it has to wait,
# but we should be close to the single chip limits
# #include <sys/ioctl.h>
# int count;
# ioctl(client_socket, FIONREAD, &count);
# printf("%d\n", count);
# TODO: the rng log gets really big, but maybe only because we were appending
# TODO: add a way to only generate until x so that we can debug
# TODO: notifications on new results?
# TODO: spin memory while fuzzing? write memory spinner

# TODO: ideas:
# we could mprotect everything but the stack
# then the os would need to copy into the buffer for us, maybe there is some way

# TODO: single step mode with these set to 1 and exit on first reproducer and verbose
# batches_of = 10000
# NOTE: above about 10000*7 we deadlock, probably because of OS buffers
# TODO: the hangs probably also come from this, probably when the other process is not dead yet
# and some os buffers are still blocked
# TODO: 1 freezes server, maybe this is the race condition, 6 too when 3 clients used, so probably we need one more than clients else deadlock
# TODO: maybe a good guideline here is 2 for each socket?

# TODO: calc based on size of batches
# for 3 clients on lab48+lab32
# in_flight_batches = 12
# else
# in_flight_batches = 2
# in_flight_batches_per_client = 5

# Because we need a lot of processes/files
raise_nofile_limit()

EXIT_FAILURE = 1

def p(*args, **kwargs):
    print(*args, **kwargs, flush=True)

class ReceivedZeroException(Exception):
    def __init__(self, message):
        super().__init__(message)

class LostClientException(Exception):
    def __init__(self, message, client, orig_exception, unrelated):
        super().__init__(message)
        self.client = client
        self.real_client = client
        self.remove_client = client
        self.orig_exception = orig_exception
        self.unrelated = unrelated

# This is an abstract class describing the capabilities of a Client.
# This is used to unify multiple cores into one client
class Client(ABC):
    @abstractmethod
    def schedule_packed_inputs(self, n_results: int, packed_inputs: bytes, priority: bool = False) -> int:
        pass

    @abstractmethod
    def get_results(self, ticket: int) -> list[Result]:
        pass

    # NOTE: they are not needed anymore. always use the execute_inputs_on_clients function
    # def schedule_inputs(self, inputs: list[InputWithRegSelect], priority: bool = False) -> int:
    #     return self.schedule_packed_inputs(len(inputs), pack_inputs(inputs), priority=priority)

    # def execute_inputs(self, inputs: list[InputWithRegSelect], priority: bool = False) -> list[Result]:
    #     return self.get_results(self.schedule_inputs(inputs, priority=priority))

class ClientMeta():
    def __init__(self, hostname, num_cpus, n_core, microarchitecture, cpu):
        self.hostname = hostname
        self.num_cpus = num_cpus
        self.n_core = n_core
        self.microarchitecture = microarchitecture
        self.cpu = cpu
        self.other = {}

    # TODO: think of where to put this
    # its good if we have a client class without the logic for loading repros etc.
    # but then multiclient also should have a way to have these attributes
    # we should probably make a second abstract class RealClient that has these attributes and then one that inherits and has the logic
    def similar(self, other) -> bool:
        if self.microarchitecture != other.microarchitecture:
            return False

        return True

    def to_dict(self, log_cpu=True):
        d = {
            # TODO: get the type from class, or just dont encode that?
            "type": "realclient",
            'hostname': self.hostname,
            'num_cpus': self.num_cpus,
            'n_core': self.n_core,
            'microarchitecture': self.microarchitecture.to_dict(),
            'tags': self.tags,
        }
        if self.vec_size:
            d['vec_size'] = self.vec_size
        d["other"] = {}
        for k, v in self.other.items():
            d["other"][k] = v
        if log_cpu:
            d['cpu'] = self.cpu.to_dict()
        return d

    @classmethod
    def from_dict(cls, data):
        microarchitecture = Microarchitecture.from_dict(data['microarchitecture'])
        cpu = None
        if "cpu" in data:
            cpu = CPUInfo.from_dict(data['cpu'])
        new = cls(
            hostname=data['hostname'],
            num_cpus=data['num_cpus'],
            n_core=data['n_core'],
            microarchitecture=microarchitecture,
            cpu=cpu,
        )
        if "vec_size" in data:
            new.vec_size = data["vec_size"]
        if "other" in data:
            new.other = data["other"]
        else:
            new.other = {}
        return new

    def __str__(self):
        return client_to_header(self)

class MultiClient(Client):
    def __init__(self, clients: list[Client]):
        self.clients = clients

        self.condition = Condition()

        self.manager = Manager()
        self.ticket_map = self.manager.dict()
        self.next_ticket = self.manager.Value("Q", 0)

    # Pass attributes through
    def __getattr__(self, name):
        return getattr(self.clients[0], name)

    def schedule_packed_inputs(self, n_results: int, packed_inputs: bytes, priority: bool = False) -> int:
        with self.condition:
            our_ticket = self.next_ticket.value
            self.next_ticket.value += 1
            client_i = our_ticket % len(self.clients)
            client = self.clients[client_i]
        # No lock here so that schedule_packed_inputs on this class can be called concurrently.
        real_ticket = client.schedule_packed_inputs(n_results, packed_inputs, priority=priority)
        with self.condition:
            self.ticket_map[our_ticket] = (real_ticket, client_i)
        return our_ticket

    def get_results(self, ticket: int) -> list[Result]:
        with self.condition:
            real_ticket, client_i = self.ticket_map[ticket]
            self.ticket_map.pop(ticket)
            client = self.clients[client_i]
        try:
            # No lock here so that get_results on this class can be called concurrently.
            res = client.get_results(real_ticket)
            for r in res:
                r.client = self
            return res
        except LostClientException as e:
            with self.condition:
                self.clients.remove(e.client)
                if len(self.clients) == 0:
                    e.remove_client = self
                else:
                    e.remove_client = None
            e.client = self
            raise e

    def to_dict(self):
        return {
            "type": "multiclient",
            "clients": [client.to_dict() for client in self.clients],
        }

    def __str__(self):
        return f"MultiClient({len(self.clients)}, 0={str(self.clients[0])} ...)"

    def __repr__(self):
        return self.__str__()

def cluster_clients_by_client_property(clients: list[Client], predicate) -> list[MultiClient]:
    classes = defaultdict(list)
    for client in clients:
        classes[predicate(client)] += [client]
    return [MultiClient(_clients) for _clients in classes.values()]

def cluster_clients_one_per_client_property(clients: list[Client], predicate) -> list[MultiClient]:
    classes = dict()
    for client in clients:
        p = predicate(client)
        if p not in classes:
            classes[p] = client
    return list(classes.values())

def group_clients_by_args(args, clients: list[Client]) -> list[Client]:
    match args.group_by:
        case 'midr':
            return cluster_clients_by_client_property(clients, lambda client: client.microarchitecture.midr)
        case 'one-per-midr':
            return cluster_clients_one_per_client_property(clients, lambda client: client.microarchitecture.midr)
        case 'hostname+microarch':
            return cluster_clients_by_client_property(clients, lambda client: client.hostname+"-"+client.microarchitecture.model_name)
        case 'hostname':
            return cluster_clients_by_client_property(clients, lambda client: client.hostname)
        case 'none':
            return clients

# NOTE: Use these functions if executing input on multiple clients.
# It's way faster since it schedules all the inputs and then gets results.
def execute_inputs_on_clients(inputs: list[Input], clients: list[Client], priority: bool = False) -> list[list[Result]]:
    assert(len(clients) > 0)
    if len(inputs) == 0:
        return len(clients)*[[]]
    packed = pack_inputs(inputs)
    tickets = [client.schedule_packed_inputs(sum(i.get_num_results() for i in inputs), packed, priority=priority) for client in clients]
    return [client.get_results(ticket) for client, ticket in zip(clients, tickets)]

def execute_input_on_clients(inp: Input, clients: list[Client], priority: bool = False) -> list[Result]:
    results = execute_inputs_on_clients([inp], clients, priority=priority)
    return [r[0] for r in results]

class RealClient(Process, Client, ClientMeta):
    # It seems like with the new architecture batches around 200 is a better choice than high.
    # This is inefficient in gen but faster with network and concurrency (?).
    # 1MB receive buffer is safe to assume on all devices. Pixel had 2MB default buffer.
    # With this and 250 batches we got nearly 60k
    # NOTE: this value has to align to the one in the client (SO_RCVBUF, SO_SNDBUF)
    #
    # Can be increased with:
    # sudo sysctl -w net.core.rmem_max=16777216 net.core.wmem_max=16777216
    #
    # echo -e "\nnet.core.rmem_max=16777216\nnet.core.wmem_max=16777216" | sudo tee -a /etc/sysctl.conf
    # max_in_flight_bytes = 1 << 20
    # because of lab57
    # TODO(now): play around with this on offline seq gen
    max_in_flight_bytes = 524288

    # TODO: we still havae broken pipe on lab08 with this

    def __init__(self, socket, address, server, single_step, single_client, compress_send, interactive=False):
        super().__init__()
        self.socket = socket
        self.address = address
        self.server = server
        self.single_step = single_step
        self.single_client = single_client
        self.interactive = interactive
        self.compress_send = compress_send

        # ClientMeta
        # This is not optimal, would need to refactor this a lot.
        self.other = {}

        self.condition = Condition()
        self.result_condition = Condition()
        self.manager = Manager()
        self.queue = self.manager.list()
        self.results = self.manager.dict()
        self.saved_exception = self.manager.dict()
        self.ready = self.manager.Value("b", False)
        self.is_dead = self.manager.Value("b", False)
        self.next_ticket = self.manager.Value("Q", 0)
        self.elf_hash = self.manager.Value(c_wchar_p, "")

        # window bits = 15+16 tells zlib to expect a gzip header.
        self.decomp = zlib.decompressobj(wbits=zlib.MAX_WBITS | 16)
        if self.compress_send:
            self.comp = zlib.compressobj(wbits=zlib.MAX_WBITS | 16)

        # TODO: find a way to move this back to run method
        # we need some way to share the cpuinfo class
        self.recv_client_info()
        self.check_hash()
        self.send_msg(struct.pack("I", self.server.bigger_max_batch_n))

        self.send_msg(struct.pack("Q", self.server.seed))

        if self.interactive:
            p('New client:', client_to_header_colored(self))

    def __str__(self):
        return f"RealClient({super().__str__()})"

    def __repr__(self):
        return self.__str__()

    def recv_client_info(self):
        self.hostname = self.recv_str()
        if self.hostname in serial_to_hostname:
            self.hostname = serial_to_hostname[self.hostname]
        self.num_cpus = struct.unpack("I", self.recv_msg(4))[0]
        # When single client is used, num_cpus should be 1
        assert(not self.single_client or self.num_cpus == 1)
        self.n_core = struct.unpack("I", self.recv_msg(4))[0]
        lscpu = self.recv_str()
        proc_cpuinfo = self.recv_str()
        sys_possible = self.recv_str()
        self.vec_size = struct.unpack("I", self.recv_msg(4))[0] or None
        match config.ARCH:
            case "aarch64":
                self.other["sve_max_size"] = struct.unpack("I", self.recv_msg(4))[0] or None
                self.other["sme_max_size"] = struct.unpack("I", self.recv_msg(4))[0] or None

        self.tags = {}
        tag_count = struct.unpack("I", self.recv_msg(4))[0] or 0
        for i in range(0, tag_count):
            tag_name = self.recv_str()
            tag_value = self.recv_str()
            self.tags[tag_name] = tag_value

        # lscpu based info
        if self.hostname in hardcoded_machine_info:
            # Get hardcoded machine info for machines that have broken lscpu
            self.cpu = hardcoded_machine_info[self.hostname]
        elif self.hostname in hardcoded_lscpu:
            self.cpu = parse_lscpu_output(hardcoded_lscpu[self.hostname])
        elif self.hostname.startswith("qemu"):
            self.cpu = qemu_config(self.hostname, self.num_cpus)
        else:
            self.cpu = parse_lscpu_output(lscpu)

        # Direct parsing of cpuinfo
        if self.hostname.startswith("qemu"):
            cpuinfos = [QemuCPUInfo(self.hostname)]*self.num_cpus
        else:
            match config.ARCH:
                case "aarch64":
                    cpuinfos = parse_cpuinfo(proc_cpuinfo, sys_possible)
                case "riscv64":
                    cpuinfos = [RiscvFakeCPUInfo(proc_cpuinfo)]*self.num_cpus

        # Get unique midrs
        # dict.fromkeys basically does set but keeps the order
        midrs = list(dict.fromkeys(c.midr() for c in cpuinfos))
        assert(len(self.cpu.microarchitectures) == len(midrs))

        for m, midr in zip(self.cpu.microarchitectures, midrs):
            m.midr = midr

        self.microarchitecture = self.cpu.get_microarchitecture_by_core_id(self.n_core)
        # The submitted number of cpus should match lscpu info
        assert(self.single_client or self.num_cpus == sum(map(lambda micro: micro.get_num_logical_cores(), self.cpu.microarchitectures)))

    def check_hash(self):
        self.elf_hash.value = self.recv_str()
        with self.server.lock:
            if not self.server.elf_hash.value:
                self.server.elf_hash.value = self.elf_hash.value
            elif self.server.elf_hash.value != self.elf_hash.value:
                p(f"Client {self.hostname} ({self.cpu.vendor} {self.microarchitecture.model_name}) has different elf hash: should be {self.server.elf_hash.value}, is {self.elf_hash.value}")
                exit(1)

    # TODO: think of how to remove this, the problem is that client needs to know how many batches, but if we only pass in
    # binary here we cant know, maybe just do packing here? But then the clients do stuff, which destroys argument of cores
    # we could restrict to cores though
    def schedule_packed_inputs(self, n_results, packed_inputs: bytes, priority: bool = False) -> int:
        # assert(n > 0 and n <= self.server.max_batch_n)

        # If this fails, reduce step size
        assert(len(packed_inputs) < self.max_in_flight_bytes)

        with self.condition:
            ticket = self.next_ticket.value
            self.next_ticket.value += 1
            entry = (ticket, n_results, packed_inputs)
            # NOTE: Never put a limit on the items that can be in the queue (or in the results) here.
            # If we add that we will probably get deadlocks.
            if priority:
                self.queue.insert(0, entry)
            else:
                self.queue.append(entry)
            self.condition.notify_all()
        return ticket

    def get_results(self, ticket: int) -> list[Result]:
        with self.result_condition:
            self.result_condition.wait_for(lambda: ticket in self.results or self.is_dead.value)
            if not ticket in self.results:
                assert(self.is_dead.value)
                # Our packet was probably not the one crashing the client because it's not at the head of the in flight list
                unrelated = self.saved_exception["tickets"] == None or ticket not in self.saved_exception["tickets"]
                raise LostClientException(f"{self.hostname} (Core {self.n_core+1}/{self.num_cpus}): {self.saved_exception['e']}", self, self.saved_exception["e"], unrelated) from self.saved_exception["e"]
            data = self.results.pop(ticket)
        return parse_results(data, self)

    def send_recv_loop(self):
        # TODO: while not terminate
        in_flight_bytes = []
        self.in_flight_inputs = []
        while True:
            while len(self.in_flight_inputs) == 0 or sum(in_flight_bytes) < self.max_in_flight_bytes:
                if self.single_step and len(self.in_flight_inputs) == 1:
                    break
                with self.condition:
                    if self.queue:
                        # Performance: Merge inputs
                        absolute_n = 0
                        inputs = []
                        packed_inputs = b""
                        pop = []
                        for i, (ticket, n, packed) in enumerate(self.queue):
                            if absolute_n+n <= self.server.bigger_max_batch_n:
                                absolute_n += n
                                inputs += [(ticket, n)]
                                packed_inputs += packed
                                pop += [i]
                        pop_shift = 0
                        for result_bytes in pop:
                            self.queue.pop(result_bytes-pop_shift)
                            pop_shift += 1
                    else:
                        # print("no data left")
                        break

                # if args.verbose:
                #     p(f"Sending out to class {c} ({hostname}): {len(packed)>>10}KB, {len(packed)}B")

                self.send_msg(struct.pack("I", absolute_n))
                # self.send_msg(struct.pack(f"{len(inputs)}I", *inputs))
                if self.compress_send:
                    self.send_msg_compressed(packed_inputs)
                else:
                    self.send_msg(packed_inputs)

                in_flight_bytes += [len(packed_inputs)]
                self.in_flight_inputs += [inputs]

            # if sum(in_flight_bytes) >= self.max_in_flight_bytes:
            #     print("im full")

            # We use a timeout here to keep the code simple. There
            # is no way in python to wait for sema or socket and checking
            # if data is in flight above ends in weird code.
            recv_ready, _, _ = select.select([self.socket], [], [], 0.3)
            if recv_ready:
                # NOTE: the reason we only handle binary data here is to keep the code simple and do
                # packing and unpacking at another part in the code
                data = self.recv_msg_compressed()

                inputs = self.in_flight_inputs.pop(0)
                in_flight_bytes.pop(0)
                with self.result_condition:
                    data_index = 0
                    for ticket, n in inputs:
                        # Extract n results from the binary data
                        d = b""
                        for _ in range(n):
                            result_bytes = struct.unpack("H", data[data_index:data_index+2])[0]
                            data_index += 2
                            d += data[data_index:data_index+result_bytes]
                            data_index += result_bytes
                        self.results[ticket] = d
                    self.result_condition.notify_all()

    def run(self):
        with self.condition:
            self.ready.value = True
            self.condition.notify_all()

        try:
            self.send_recv_loop()
        except (ConnectionResetError, TimeoutError, BrokenPipeError, ReceivedZeroException, OSError) as e:
            self.saved_exception["e"] = e
            if len(self.in_flight_inputs) > 0:
                self.saved_exception["tickets"] = [inp[0] for inp in self.in_flight_inputs[0]]
            else:
                self.saved_exception["tickets"] = None
        with self.result_condition:
            self.is_dead.value = True
            self.result_condition.notify_all()

    def recv_all(self, n):
        buf = bytearray(n)
        view = memoryview(buf)
        received = 0

        while received < n:
            r = self.socket.recv_into(view[received:])
            if r == 0:
                raise ReceivedZeroException("Received 0 bytes, the connection likely closed")
            received += r

        assert(received == n)

        return buf

    def recv_hdr(self):
        n = struct.unpack('I', self.recv_all(4))[0]
        return n

    def recv_msg_compressed(self):

        compressed = self.recv_msg()
        uncompressed = self.decomp.decompress(compressed)

        return uncompressed

    def recv_msg(self, max_size=None):

        n = self.recv_hdr()

        if max_size and n > max_size:
            print(self.recv_all(n))
            raise Exception('msg > max_size')

        return self.recv_all(n)

    def recv_str(self):
        return self.recv_msg().decode("utf-8")

    def recv_array(self):
        binary = self.recv_msg()
        return struct.unpack(f"{len(binary)//8}Q", binary)

    def send_msg(self, buf):
        n = len(buf)
        header = struct.pack('I', n)

        self.socket.sendall(header)
        self.socket.sendall(buf)

    def send_msg_compressed(self, buf):
        compressed = self.comp.compress(buf)
        compressed += self.comp.flush(zlib.Z_SYNC_FLUSH)
        self.send_msg(compressed)

    def send_str(self, msg):
        self.send_msg(msg.encode("utf-8"))

# Thread so that clients are in the same memory space.
class Server(Thread):
    # TODO: make single_step mandatory and add to common arg parser somehow?
    def __init__(self, bind_addr, port, expect, max_batch_n, compress_send=True, single_step=False, single_client=False, seed=0, interactive=True) -> None:
        super().__init__()

        self.bind_addr = bind_addr or "0.0.0.0"
        self.port = port or 0
        self.max_batch_n = max_batch_n
        self.seed = seed
        # NOTE: add some space for merging smaller inputs
        self.bigger_max_batch_n = 2*max_batch_n-1
        self.expect = expect
        self.clients = []
        self.single_step = single_step
        self.single_client = single_client
        self.interactive = interactive
        self.compress_send = compress_send

        # Protected by lock *until start finished*:
        self.lock = Lock()
        self.manager = Manager()
        self.elf_hash = self.manager.Value(c_wchar_p, "")

        self.port_condition = threading.Condition()

    def get_port(self):
        with self.port_condition:
            self.port_condition.wait_for(lambda: self.port)
            return self.port

    def run(self):
        server_ip = self.bind_addr or self.get_ip()

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_socket.bind((self.bind_addr, self.port))
            with self.port_condition:
                self.port = server_socket.getsockname()[1]
                self.port_condition.notify_all()

            server_socket.listen(100)

            if self.interactive:
                p(f'Server listening on {server_ip} port {self.port}...')

                p('Connect with:')
                p(f'diffuzz-client {server_ip} {self.port}')

                if self.expect:
                    p(f'Waiting for {self.expect} machines...')
                p('Press Enter to begin.')
            while True:
                listen_on = [server_socket]
                if self.interactive:
                    listen_on += [sys.stdin]
                inputs, _, _ = select.select(listen_on, [], [])

                for ready_socket in inputs:
                    if ready_socket == sys.stdin:
                        server_socket.shutdown(socket.SHUT_RDWR)
                        server_socket.close()
                        return
                    elif ready_socket == server_socket:
                        client_socket, client_address = server_socket.accept()

                        client = RealClient(client_socket, client_address, self, self.single_step, self.single_client, self.compress_send, interactive=self.interactive)
                        client.start()

                        self.clients += [client]

                        with client.condition:
                            client.condition.wait_for(lambda: client.ready.value)

                        # Are all machines connected? Are all cores per machine connected?
                        if self.expect != 0:
                            machine_to_n_cores = {c.hostname: c.num_cpus for c in self.clients}
                            n_awaited_clients = sum(machine_to_n_cores.values())
                            machines = machine_to_n_cores.keys()
                            if len(machines) == self.expect and len(self.clients) == n_awaited_clients:
                                server_socket.shutdown(socket.SHUT_RDWR)
                                server_socket.close()
                                return

    def get_ip(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        finally:
            s.close()
        return ip
