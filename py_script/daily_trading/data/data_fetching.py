import psycopg2
import pandas as pd
from sqlalchemy import create_engine

def fetch_data_from_db(symbols, db_config, start_date, end_date, chunk_size=10000):
    engine = create_engine(f"postgresql://{db_config['user']}:{db_config['password']}@{db_config['host']}:{db_config['port']}/{db_config['dbname']}")
    with engine.connect() as conn:
        for symbol in symbols:
            query = f"SELECT date, symbol, close, volume FROM {symbol} WHERE date >= '{start_date}' AND date <= '{end_date}'"
            for chunk in pd.read_sql(query, conn, chunksize=chunk_size):
                yield symbol, chunk

def fetch_data_from_memory(data):
    for symbol in data['symbol'].unique():
        yield data[data['symbol'] == symbol]

def fetch_data(symbols, db_config, load_from_memory=False, data=None):
    if load_from_memory:
        return fetch_data_from_memory(data)
    else:
        return fetch_data_from_db(symbols, db_config)