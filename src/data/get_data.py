import pandas as pd
import numpy as np
import os
from datetime import datetime, timedelta
from ib_insync import IB, Stock, util  # Interactive Brokers API

# Connect to Interactive Brokers TWS
ib = IB()
ib.connect('127.0.0.1', 7496, clientId=1)

# Define data paths
original_data_path = 'data/original_data'
daily_data_path = 'data/daily_data'

# Create directories if they don't exist
os.makedirs(original_data_path, exist_ok=True)
os.makedirs(daily_data_path, exist_ok=True)

# Check existing data files
files = sorted([f for f in os.listdir(daily_data_path) if f.endswith('.csv')])
if files:
    last_file = files[-1]
    last_date = pd.to_datetime(last_file.split('_')[-1].replace('.csv', ''))
    start_date = last_date + timedelta(days=1)
else:
    start_date = datetime.now() - timedelta(days=120)

end_date = datetime.now()

# Fetch historical data from IB
def fetch_historical_data(symbol, start, end, max_retries=3):
    contract = Stock(symbol, 'SMART', 'USD')
    retry_count = 0
    all_data = []

    # Splitting the request into smaller chunks
    delta_days = 30  # Request 30 days at a time
    current_end = start

    while current_end < end:
        current_start = current_end
        current_end = min(current_end + timedelta(days=delta_days), end)

        while retry_count < max_retries:
            try:
                bars = ib.reqHistoricalData(
                    contract,
                    endDateTime=current_end,
                    durationStr='30 D',  # Requesting 30 days of data
                    barSizeSetting='1 min',
                    whatToShow='TRADES',
                    useRTH=True
                )
                if bars:
                    df = util.df(bars)
                    df['Datetime'] = pd.to_datetime(df['date'])
                    df.set_index('Datetime', inplace=True)
                    df = df[['open', 'high', 'low', 'close', 'volume']]
                    df.columns = ['Open', 'High', 'Low', 'Close', 'Volume']
                    all_data.append(df)
                    print(f"Successfully fetched data for {symbol} from {current_start} to {current_end}")
                    break  # Exit retry loop on success
                else:
                    print(f"No data received for {symbol} from {current_start} to {current_end}, retrying...")
            except Exception as e:
                print(f"Error fetching data: {e}, retrying...")
                retry_count += 1
                ib.sleep(5)  # Wait for 5 seconds before retrying

        if retry_count >= max_retries:
            print(f"Failed to fetch data for {symbol} from {current_start} to {current_end} after {max_retries} retries.")
            break

    if all_data:
        df = pd.concat(all_data)
        df = df.sort_index()
        return df
    else:
        return pd.DataFrame()

# Fetch data and save original data
df = fetch_historical_data('SPY', start_date, end_date)

if not df.empty:
    original_file_path = os.path.join(original_data_path, f"SPY_original_{start_date.date()}_to_{end_date.date()}.csv")
    df.to_csv(original_file_path)
    print(f"Original data saved to {original_file_path}")

    # Feature engineering
    def feature_engineering(df):
        df['Previous_Close'] = df['Close'].shift(1)
        df['Gap'] = df['Open'] - df['Previous_Close']
        df['SMA_20'] = df['Close'].rolling(window=20).mean()
        df['EMA_12'] = df['Close'].ewm(span=12, adjust=False).mean()
        df['RSI'] = compute_rsi(df['Close'], window=14)
        df['MACD'], df['Signal'] = compute_macd(df['Close'])
        df['Upper_BB'], df['Lower_BB'] = compute_bollinger_bands(df['Close'])
        df['Volume_MA_20'] = df['Volume'].rolling(window=20).mean()
        
        # Drop the first row of each day because of NaN values from 'Previous_Close'
        df.dropna(subset=['Previous_Close'], inplace=True)
        df.dropna(subset=['Gap'], inplace=True)
        df.dropna(subset=['SMA_20'], inplace=True)
        df.dropna(subset=['EMA_12'], inplace=True)
        df.dropna(subset=['RSI'], inplace=True)
        df.dropna(subset=['MACD'], inplace=True)
        df.dropna(subset=['Signal'], inplace=True)
        df.dropna(subset=['Upper_BB'], inplace=True)
        df.dropna(subset=['Lower_BB'], inplace=True)
        df.dropna(subset=['Volume_MA_20'], inplace=True)
        
        return df

    def compute_rsi(series, window):
        delta = series.diff()
        gain = (delta.where(delta > 0, 0)).rolling(window=window).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(window=window).mean()
        rs = gain / loss
        rsi = 100 - (100 / (1 + rs))
        return rsi

    def compute_macd(series, fast=12, slow=26, signal=9):
        fast_ema = series.ewm(span=fast, adjust=False).mean()
        slow_ema = series.ewm(span=slow, adjust=False).mean()
        macd = fast_ema - slow_ema
        signal = macd.ewm(span=signal, adjust=False).mean()
        return macd, signal

    def compute_bollinger_bands(series, window=20, num_std=2):
        sma = series.rolling(window=window).mean()
        std = series.rolling(window=window).std()
        upper_band = sma + (num_std * std)
        lower_band = sma - (num_std * std)
        return upper_band, lower_band

    df = feature_engineering(df)

    # Save data by day
    for date, group in df.groupby(df.index.date):
        daily_file_path = os.path.join(daily_data_path, f"SPY_intraday_{date}.csv")
        group.to_csv(daily_file_path)
        print(f"Daily data for {date} saved to {daily_file_path}")

else:
    print("No data available to process.")