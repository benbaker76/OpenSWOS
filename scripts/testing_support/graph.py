#!/usr/bin/env python3
"""Declarative screen graph + plain walker (docs/TESTING_FLOWS_DESIGN.md,
"Action registry and declarative screen graph"). PURE — no debugger I/O.

Nodes identify screen families (onInit symbol); edges carry the action that
traverses them and the completion predicate that proves it. v1 is ordinary
BFS path finding over declared edges — no precondition/effect search engine.
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional


@dataclass
class Screen:
    name: str
    init: Optional[str]          # onInit symbol; None = not yet pinned live
    note: str = ""


@dataclass
class Edge:
    name: str
    src: str
    dst: str
    action: dict                 # executor action template; "$param" slots
    completion: dict             # executor predicate template
    timeout: float = 20.0
    max_attempts: int = 12
    safe_retry: bool = True


class ScreenGraph:
    def __init__(self, screens: List[Screen], edges: List[Edge]):
        self.screens: Dict[str, Screen] = {s.name: s for s in screens}
        self.edges: List[Edge] = edges
        self._by_init = {s.init: s.name for s in screens if s.init}

    def node_of(self, snapshot) -> Optional[str]:
        """Which declared screen a snapshot is, by onInit symbol. None for an
        undeclared screen — a capability gap, not a guess."""
        return self._by_init.get(snapshot.screen)

    def find_path(self, src: str, dst: str) -> Optional[List[Edge]]:
        """Plain BFS over declared edges; None when no declared route exists
        (the caller reports a capability error, never explores)."""
        if src == dst:
            return []
        seen = {src}
        queue = [(src, [])]
        while queue:
            node, path = queue.pop(0)
            for e in self.edges:
                if e.src == node and e.dst not in seen:
                    np = path + [e]
                    if e.dst == dst:
                        return np
                    seen.add(e.dst)
                    queue.append((e.dst, np))
        return None
