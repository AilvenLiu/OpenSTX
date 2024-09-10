import pandas as pd

def preprocess_data(df):
    print("Columns before preprocessing:", df.columns)
    df['date'] = pd.to_datetime(df['date'])
    df.set_index('date', inplace=True)
    df.sort_index(inplace=True)
    df.dropna(inplace=True)
    print("Columns after preprocessing:", df.columns)
    return df

def preprocess_symbol_data(df, symbol):
    symbol_data = df[df['symbol'] == symbol].copy()
    print("Columns before preprocessing symbol data:", symbol_data.columns)
    symbol_data['date'] = pd.to_datetime(symbol_data.index)
    symbol_data.set_index('date', inplace=True)
    symbol_data.sort_index(inplace=True)
    symbol_data.dropna(inplace=True)
    print("Columns after preprocessing symbol data:", symbol_data.columns)
    return symbol_data