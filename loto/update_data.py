#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Met à jour data/loto_tirages.csv avec les derniers tirages publiés par la FDJ.

À lancer depuis une machine avec accès internet normal (le fichier officiel
couvre tous les tirages depuis le 06/11/2019 et est réactualisé après chaque
tirage) :

    python3 update_data.py

Source officielle : https://media.fdj.fr/static/csv/loto/loto_201911.zip
"""

import csv
import io
import os
import urllib.request
import zipfile
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data", "loto_tirages.csv")
URL = "https://media.fdj.fr/static/csv/loto/loto_201911.zip"


def load_existing():
    rows = {}
    with open(DATA, encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter=";"):
            rows[row["date"]] = row
    return rows


def fetch_fdj():
    req = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        payload = resp.read()
    zf = zipfile.ZipFile(io.BytesIO(payload))
    name = next(n for n in zf.namelist() if n.endswith(".csv"))
    text = zf.read(name).decode("utf-8", errors="replace")
    out = {}
    for row in csv.DictReader(io.StringIO(text), delimiter=";"):
        d = datetime.strptime(row["date_de_tirage"].strip(), "%d/%m/%Y").date()
        balls = sorted(int(row[f"boule_{i}"]) for i in range(1, 6))
        assert all(1 <= b <= 49 for b in balls) and len(set(balls)) == 5
        chance = int(row["numero_chance"])
        assert 1 <= chance <= 10
        out[d.isoformat()] = {
            "date": d.isoformat(),
            "jour": row.get("jour_de_tirage", "").strip(),
            **{f"b{i+1}": str(b) for i, b in enumerate(balls)},
            "chance": str(chance),
        }
    return out


def main():
    existing = load_existing()
    fresh = fetch_fdj()
    added = [d for d in fresh if d not in existing]
    existing.update(fresh)
    with open(DATA, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["date", "jour", "b1", "b2", "b3", "b4", "b5", "chance"])
        for d in sorted(existing):
            r = existing[d]
            w.writerow([r["date"], r["jour"], r["b1"], r["b2"], r["b3"],
                        r["b4"], r["b5"], r["chance"]])
    print(f"{len(added)} nouveau(x) tirage(s) ajouté(s) ; "
          f"total {len(existing)} tirages.")
    if added:
        print("Derniers ajoutés :", ", ".join(sorted(added)[-5:]))


if __name__ == "__main__":
    main()
