#!/usr/bin/env python3
from collections import defaultdict

from pyutils.server import Client, RealClient
from pyutils.result import Result
from pyutils.util import client_to_header

def clients_from_client_tuples(client_tuples):
    return [x[0] for x in client_tuples]

def real_clients_from_client_tuples(client_tuples):
    return [x[1] for x in client_tuples]

def cluster_clients_by_results(results: list[Result]) -> dict[Result, list[tuple[Client, RealClient]]]:
    result_to_clients: dict[Result, list[tuple[Client, RealClient]]] = defaultdict(list[tuple[Client, RealClient]])
    for result in results:
        result_to_clients[result] += [(result.client, result.real_client)]
    return result_to_clients

def result_to_clients_to_str(result_to_clients, color=True):
    cells = []
    for result, clients_tuples in result_to_clients.items():
        cell = ", ".join(map(client_to_header, clients_tuples))+"\n"
        cell += result.__str__(color=color)
        cells += [cell]
    return "\n--------------------------------------------------------------------------------\n".join(cells)
