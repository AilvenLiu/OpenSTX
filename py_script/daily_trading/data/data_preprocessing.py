import pandas as pd

def preprocess_data(df):
    df['date'] = pd.to_datetime(df['date'])
    df.set_index('date', inplace=True)
    df.sort_index(inplace=True)
    df.dropna(inplace=True)
    return df

def preprocess_symbol_data(df, symbol):
    symbol_data = df[df['symbol'] == symbol].copy()
    symbol_data = preprocess_data(symbol_data)
    return symbol_data