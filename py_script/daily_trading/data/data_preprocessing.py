import pandas as pd

def preprocess_data(df):
    df['date'] = pd.to_datetime(df['date'])
    df.set_index('date', inplace=True)
    df.sort_index(inplace=True)
    df.index.freq = pd.infer_freq(df.index)  # Set frequency
    return df

def preprocess_symbol_data(df, symbol):
    symbol_data = df[df['symbol'] == symbol].copy()
    symbol_data['date'] = pd.to_datetime(symbol_data.index)
    symbol_data.set_index('date', inplace=True)
    symbol_data.sort_index(inplace=True)
    symbol_data.dropna(inplace=True)
    return symbol_data