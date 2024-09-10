import psycopg2
import pandas as pd
from sqlalchemy import create_engine

def fetch_data_from_db(symbols, db_config, chunk_size=10000):
    engine = create_engine(f"postgresql://{db_config['user']}:{db_config['password']}@{db_config['host']}:{db_config['port']}/{db_config['dbname']}")
    query = f"""
    SELECT date, symbol, open, high, low, close, volume, adj_close, sma, ema, rsi, macd, vwap, momentum
    FROM daily_data
    WHERE symbol IN ({','.join([f"'{symbol}'" for symbol in symbols])})
    ORDER BY date DESC;
    """
    with engine.connect() as conn:
        for chunk in pd.read_sql(query, conn, chunksize=chunk_size):
            yield chunk

def fetch_data_from_memory(data):
    for symbol in data['symbol'].unique():
        yield data[data['symbol'] == symbol]

def fetch_data(symbols, db_config, load_from_memory=False, data=None):
    if load_from_memory:
        return fetch_data_from_memory(data)
    else:
        return fetch_data_from_db(symbols, db_config)