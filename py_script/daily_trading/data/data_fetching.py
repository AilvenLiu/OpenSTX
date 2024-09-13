import psycopg2
import pandas as pd
from sqlalchemy import create_engine
import time
from tenacity import retry, stop_after_attempt, wait_exponential
import logging

@retry(stop=stop_after_attempt(5), wait=wait_exponential(multiplier=1, min=4, max=10))
def fetch_data_from_db(symbols, db_config, start_date, end_date, chunk_size=10000):
    engine = create_engine(f"postgresql://{db_config['user']}:{db_config['password']}@{db_config['host']}:{db_config['port']}/{db_config['dbname']}", pool_pre_ping=True, pool_recycle=3600)
    try:
        with engine.connect() as conn:
            for symbol in symbols:
                query = f"SELECT date, symbol, close, volume FROM daily_data WHERE symbol = '{symbol}' AND date >= '{start_date}' AND date <= '{end_date}'"
                for chunk in pd.read_sql(query, conn, chunksize=chunk_size):
                    yield symbol, chunk
    except Exception as e:
        logger.error(f"Error fetching data: {str(e)}")
        raise
    finally:
        engine.dispose()

def fetch_data_from_memory(data):
    for symbol in data['symbol'].unique():
        yield data[data['symbol'] == symbol]

def fetch_data(symbols, db_config, load_from_memory=False, data=None):
    if load_from_memory:
        return fetch_data_from_memory(data)
    else:
        return fetch_data_from_db(symbols, db_config)