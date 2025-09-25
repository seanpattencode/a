#!/usr/bin/env python3
from fastapi import FastAPI, Request
import sys
sys.path.append('/home/seanpatten/projects/AIOS/core')
import aios_db
import uvicorn

app = FastAPI()

@app.get("/data/{name}")
async def get_data(name: str):
    return aios_db.read(name)

@app.post("/data/{name}")
async def post_data(name: str, request: Request):
    return aios_db.write(name, await request.json())

@app.post("/event/{target}")
async def emit_event(target: str, request: Request):
    aios_db.execute("events",
                    "CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY, target TEXT, data TEXT, created TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")
    aios_db.execute("events", "INSERT INTO events(target, data) VALUES (?, ?)",
                    (target, (await request.body()).decode()))
    return {"status": "ok"}

@app.get("/status")
async def status():
    return {k: aios_db.read(k) for k in ["services", "tasks", "schedule"]}

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)