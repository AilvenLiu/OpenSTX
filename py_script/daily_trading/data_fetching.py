import psycopg2
import pandas as pd
from db_config import read_db_config

def fetch_data_from_db(symbols, db_config, chunk_size=10000):
    conn = psycopg2.connect(**db_config)
    query = f"""
    SELECT * FROM daily_data
    WHERE symbol IN ({','.join([f"'{symbol}'" for symbol in symbols])})
    ORDER BY date DESC;
    """
    for chunk in pd.read_sql(query, conn, chunksize=chunk_size):
        yield chunk
    conn.close()

def fetch_data_from_memory(data):
    for symbol in data['symbol'].unique():
        yield data[data['symbol'] == symbol]

def fetch_data(symbols, db_config, load_from_memory=False, data=None):
    if load_from_memory:
        return fetch_data_from_memory(data)
    else:
        return fetch_data_from_db(symbols, db_config)