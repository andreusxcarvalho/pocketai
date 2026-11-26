"""Full-text search across all sessions (SQLite FTS5)."""

from __future__ import annotations

import sqlite3
from pathlib import Path

SCHEMA = """
CREATE VIRTUAL TABLE IF NOT EXISTS chunks USING fts5(
  session UNINDEXED, start UNINDEXED, kind UNINDEXED, text, tokenize='porter unicode61');
"""


class SearchIndex:
    def __init__(self, path: Path):
        self.db = sqlite3.connect(path)
        self.db.executescript(SCHEMA)

    def replace_session(self, session: str, rows: list[tuple[float, str, str]]) -> None:
        """rows = (start seconds, kind 'speech'|'slide', text)."""
        self.db.execute("DELETE FROM chunks WHERE session = ?", (session,))
        self.db.executemany("INSERT INTO chunks(session, start, kind, text) VALUES (?,?,?,?)",
                            [(session, s, k, t) for s, k, t in rows if t.strip()])
        self.db.commit()

    def search(self, query: str, limit: int = 20) -> list[dict]:
        cur = self.db.execute(
            "SELECT session, start, kind, snippet(chunks, 3, '[', ']', ' … ', 12) "
            "FROM chunks WHERE chunks MATCH ? ORDER BY rank LIMIT ?", (query, limit))
        return [{"session": s, "start": float(t), "kind": k, "snippet": snip} for s, t, k, snip in cur]
